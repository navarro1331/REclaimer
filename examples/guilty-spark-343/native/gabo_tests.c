#define _CRT_SECURE_NO_WARNINGS
#include "spark.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){printf("FAIL gabo:%d: %s (%s)\n",__LINE__,#x,error);goto fail;}passed++;}while(0)
static void turn(cJSON *source,const char *threadId,const char *model,const char *user,double ts){
    cJSON *t=find_id(get(source,"threads"),threadId);if(!t){t=cJSON_CreateObject();setstr(t,"id",threadId);setstr(t,"name","Synthetic imported conversation");cJSON_AddArrayToObject(t,"messages");cJSON_AddItemToArray(get(source,"threads"),t);}
    cJSON *u=cJSON_CreateObject();setstr(u,"role","user");setstr(u,"content",user);setnum(u,"timestamp",ts-5);cJSON_AddItemToArray(get(t,"messages"),u);
    cJSON *a=cJSON_CreateObject();setstr(a,"role","assistant");setstr(a,"content","Assistant text must never become a user memory.");setnum(a,"timestamp",ts);cJSON_AddItemToArray(get(t,"messages"),a);
    cJSON *r=cJSON_CreateObject();setstr(r,"thread_id",threadId);setstr(r,"model_id",model);setnum(r,"ts",ts-3);cJSON_AddItemToArray(get(source,"telemetry"),r);
}
static int owned(const cJSON *s,const char *model){int n=0;cJSON *t;cJSON_ArrayForEach(t,get(s,"threads"))if(!strcmp(str(t,"model"),model)&&get(t,"messages")->child)n++;return n;}
int gabo_tests(void){
    int passed=0;char error[512]={0};GaboScan scan;cJSON *state=new_state(),*source=cJSON_Parse("{\"threads\":[],\"telemetry\":[]}"),*aliases=cJSON_Parse("{\"Alpha\":\"alpha:latest\",\"Beta\":\"beta:v2\"}"),*next=NULL;
    CHECK(select_model(state,"alpha:latest",error));setstr(get(state,"threads")->child,"draft","An unsent draft");char *active=_strdup(str(state,"activeThreadId"));
    turn(source,"one","Alpha","My name is Ada.",100000);turn(source,"two","Beta","I prefer detailed explanations.",200000);
    next=gabo_merge(state,source,"https://source/",aliases,&scan,error);CHECK(next);CHECK(scan.added==2&&scan.messages==4&&scan.memories==2);CHECK(!strcmp(str(next,"selectedModel"),"alpha:latest"));CHECK(!strcmp(str(next,"activeThreadId"),active));CHECK(!strcmp(str(find_id(get(next,"threads"),active),"draft"),"An unsent draft"));CHECK(owned(next,"alpha:latest")==1&&owned(next,"beta:v2")==1);CHECK(cJSON_GetArraySize(get(get(get(next,"modelMemories"),"alpha:latest"),"notes"))==1);CHECK(cJSON_GetArraySize(get(get(get(next,"modelMemories"),"beta:v2"),"notes"))==1);
    cJSON_Delete(state);state=next;next=gabo_merge(state,source,"https://source/",aliases,&scan,error);CHECK(next&&!scan.changed&&!scan.added&&!scan.memories);cJSON_Delete(next);next=NULL;
    CHECK(set_memories(state,"",1,1,error));turn(source,"one","Alpha","I use a telescope.",300000);
    next=gabo_merge(state,source,"https://source/",aliases,&scan,error);CHECK(next&&scan.updated==1&&scan.added==0&&scan.messages==2&&scan.memories==1);CHECK(cJSON_GetArraySize(memory_notes(next))==1);CHECK(!strcmp(memory_notes(next)->child->valuestring,"I use a telescope."));cJSON_Delete(state);state=next;
    // Local continuation and a simultaneous source append create a new branch.
    cJSON *record=get(state,"gaboImports")->child,*local=find_id(get(state,"threads"),str(record,"destinationId"));CHECK(local);cJSON *m=cJSON_CreateObject();setstr(m,"id","local-message");setstr(m,"role","user");setstr(m,"content","Local continuation");cJSON_AddItemToArray(get(local,"messages"),m);
    turn(source,"one","Alpha","I like astronomy.",400000);next=gabo_merge(state,source,"https://source/",aliases,&scan,error);CHECK(next&&scan.added==1&&scan.conflicts==1&&scan.memories==1);CHECK(owned(next,"alpha:latest")==2);CHECK(find_id(get(next,"threads"),str(local,"id"))!=NULL);cJSON_Delete(state);state=next;
    // A chat can use multiple models; each receives only its own turn pairs.
    turn(source,"one","Beta","I prefer short replies.",500000);next=gabo_merge(state,source,"https://source/",aliases,&scan,error);CHECK(next&&scan.added==1&&scan.memories==1);CHECK(owned(next,"beta:v2")==2);cJSON_Delete(state);state=next;
    turn(source,"unknown","Unregistered","My name is Wrong.",600000);next=gabo_merge(state,source,"https://source/",aliases,&scan,error);CHECK(next&&scan.pending==1&&!scan.added&&!scan.memories);cJSON_Delete(next);next=NULL;
    // Disabled automatic memory capture is respected and not replayed on enable.
    CHECK(set_memories(state,"",1,0,error));turn(source,"one","Alpha","I live in Chicago.",700000);next=gabo_merge(state,source,"https://source/",aliases,&scan,error);CHECK(next&&scan.memories==0&&cJSON_GetArraySize(memory_notes(next))==0);cJSON_Delete(state);state=next;CHECK(set_memories(state,"",1,1,error));next=gabo_merge(state,source,"https://source/",aliases,&scan,error);CHECK(next&&!scan.memories);cJSON_Delete(next);next=NULL;
    // Deleting an imported destination suppresses reimport, including new turns.
    record=get(state,"gaboImports")->child;local=find_id(get(state,"threads"),str(record,"destinationId"));CHECK(local);cJSON_Delete(cJSON_DetachItemViaPointer(get(state,"threads"),local));turn(source,"one","Alpha","I like rain.",800000);next=gabo_merge(state,source,"https://source/",aliases,&scan,error);CHECK(next&&scan.skipped>=1&&!scan.memories);cJSON_Delete(state);state=next;
    // Journal roundtrip keeps the same idempotence decisions after restart.
    char *json=cJSON_PrintUnformatted(state);next=import_state(json,NULL,error);free(json);CHECK(next&&cJSON_GetArraySize(get(next,"gaboImports"))==3);cJSON_Delete(state);state=next;
    next=gabo_merge(state,source,"https://source/",aliases,&scan,error);CHECK(next&&!scan.changed&&!scan.memories);cJSON_Delete(next);next=NULL;
    // Ambiguous telemetry must not silently assign either model.
    turn(source,"ambiguous","Alpha","My name is Shared.",900000);cJSON *r=cJSON_CreateObject();setstr(r,"thread_id","ambiguous");setstr(r,"model_id","Beta");setnum(r,"ts",899998);cJSON_AddItemToArray(get(source,"telemetry"),r);next=gabo_merge(state,source,"https://source/",aliases,&scan,error);CHECK(next&&scan.pending>=2&&!scan.added);cJSON_Delete(next);next=NULL;
    // An expired telemetry entry must not be interpreted as a truncated source.
    cJSON_DeleteItemFromArray(get(source,"telemetry"),0);next=gabo_merge(state,source,"https://source/",aliases,&scan,error);CHECK(next&&!scan.changed&&!scan.added&&!scan.memories&&scan.pending>=3);cJSON_Delete(next);next=NULL;
    free(active);cJSON_Delete(state);cJSON_Delete(source);cJSON_Delete(aliases);return passed;
fail:cJSON_Delete(next);cJSON_Delete(state);cJSON_Delete(source);cJSON_Delete(aliases);return -1;
}
