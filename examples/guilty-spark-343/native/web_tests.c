#define _CRT_SECURE_NO_WARNINGS
#include "spark.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){printf("FAIL web:%d: %s (%s)\n",__LINE__,#x,error);goto fail;}passed++;}while(0)
int web_tests(void){
    int passed=0;char error[512]={0};cJSON *s=new_state(),*p=NULL,*next=NULL,*params=NULL;
    CHECK(select_model(s,"alpha",error));cJSON *a=get(s,"threads")->child;char id[128];strcpy(id,str(a,"id"));
    CHECK(set_memories(s,"I like astronomy.",1,1,error));CHECK(select_model(s,"beta",error));CHECK(set_memories(s,"I like jazz.",1,1,error));
    setstr(s,"persona","User description");p=web_profile(s,"alpha");CHECK(cJSON_GetArraySize(get(p,"threads"))==1);CHECK(!strcmp(str(get(p,"personaCards")->child,"description"),"User description"));
    cJSON *t=get(p,"threads")->child,*m=cJSON_CreateObject();setstr(m,"id","message-1");setstr(m,"role","user");setstr(m,"content","I use a telescope.");cJSON_AddItemToArray(get(t,"messages"),m);
    cJSON *variant=cJSON_CreateObject();setstr(variant,"content","Earlier wording");cJSON_AddItemToArray(cJSON_AddArrayToObject(m,"variants"),variant);
    setstr(get(p,"settings"),"apiKey","synthetic-key-must-not-persist");cJSON *schedules=cJSON_AddArrayToObject(p,"schedules"),*schedule=cJSON_CreateObject();setstr(schedule,"id","daily");setstr(schedule,"threadId",id);setstr(schedule,"prompt","Scheduled text");cJSON_AddItemToArray(schedules,schedule);
    cJSON *card=cJSON_CreateObject();setstr(card,"id","portrait");setstr(card,"name","Portrait");setstr(card,"ragStorybook","# stars\ntags: astronomy [1.0]\nA private observatory.");char *avatar=allocate(300001);memset(avatar,'a',300000);setstr(card,"avatar",avatar);free(avatar);cJSON_AddItemToArray(get(p,"characterCards"),card);
    next=web_save_profile(s,p,"alpha",error);CHECK(next);CHECK(!strcmp(str(next,"selectedModel"),"beta"));CHECK(cJSON_GetArraySize(get(get(next,"modelMemories"),"alpha"))>0);char *notes=cJSON_PrintUnformatted(get(get(next,"modelMemories"),"beta"));CHECK(!strstr(notes,"telescope"));free(notes);
    cJSON_Delete(s);s=next;next=NULL;cJSON_Delete(p);p=web_profile(s,"alpha");CHECK(!*str(get(p,"settings"),"apiKey"));CHECK(cJSON_GetArraySize(get(p,"schedules"))==1);CHECK(strlen(str(get(p,"characterCards")->child,"avatar"))==300000);CHECK(get(get(get(p,"threads")->child,"messages")->child,"variants")!=NULL);
    next=web_save_profile(s,p,"alpha",error);CHECK(next);cJSON_Delete(next);next=NULL;
    t=get(p,"threads")->child;cJSON_AddItemToArray(get(t,"messages"),cJSON_Duplicate(get(t,"messages")->child,1));CHECK(!web_save_profile(s,p,"alpha",error));cJSON_DeleteItemFromArray(get(t,"messages"),1);
    cJSON_AddItemToArray(get(p,"threads"),cJSON_Duplicate(t,1));CHECK(!web_save_profile(s,p,"alpha",error));cJSON_DeleteItemFromArray(get(p,"threads"),1);
    cJSON_Delete(p);p=cJSON_Parse("{\"top_k\":20,\"top_p\":0.8,\"dry_multiplier\":0.5,\"logit_bias\":{\"123\":-20},\"stop\":[\"STOP\"]}");params=generation_parameters(p,0,error);CHECK(params&&get(params,"dry_multiplier")&&get(params,"logit_bias"));cJSON_Delete(params);params=NULL;CHECK(!generation_parameters(p,1,error));setnum(p,"top_k",1.5);CHECK(!generation_parameters(p,0,error));
    cJSON_Delete(p);p=cJSON_Parse("{\"top_k\":30,\"top_p\":0.8,\"dry_multiplier\":0,\"dry_base\":1.75,\"xtc_probability\":0}");params=generation_parameters(p,1,error);CHECK(params&&get(params,"top_k")&&!get(params,"dry_base"));cJSON_Delete(params);cJSON_Delete(p);cJSON_Delete(s);return passed;
fail:cJSON_Delete(params);cJSON_Delete(next);cJSON_Delete(p);cJSON_Delete(s);return -1;
}
