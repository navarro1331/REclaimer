#define _CRT_SECURE_NO_WARNINGS
#include "spark.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){printf("FAIL activity:%d: %s (%s)\n",__LINE__,#x,error);goto fail;}passed++;}while(0)
static cJSON *source(void){return cJSON_Parse("{\"threads\":[{\"id\":\"a\",\"model\":\"alpha\",\"messages\":[{\"role\":\"user\",\"content\":\"I like astronomy.\"},{\"role\":\"assistant\",\"content\":\"My name is Assistant-only.\"}]},{\"id\":\"b\",\"model\":\"beta\",\"messages\":[{\"role\":\"user\",\"content\":\"I prefer short replies.\"},{\"role\":\"assistant\",\"content\":\"Acknowledged.\"}]}]}");}
int activity_tests(void){
    int passed=0;char error[512]={0};GaboScan scan;cJSON *state=new_state(),*src=source(),*aliases=cJSON_Parse("{\"alpha\":\"alpha\",\"beta\":\"beta\"}"),*next=NULL,*check=cJSON_Parse("{\"lastCheckUtc\":\"2026-10-23T20:00:00Z\",\"lastSuccessfulCheckUtc\":\"2026-10-23T20:00:00Z\",\"succeeded\":true,\"summary\":\"Checked successfully.\"}");char *text=NULL,*json=NULL;
    CHECK(activity_time_valid("2024-02-29T00:00:00Z"));CHECK(!activity_time_valid("2025-02-29T00:00:00Z"));CHECK(!activity_time_valid("2026-10-23T00:00:00X"));CHECK(!activity_time_valid("2026-13-01T00:00:00Z"));CHECK(!activity_time_valid("2026-01-01T24:00:00Z"));
    CHECK(select_model(state,"alpha",error));next=gabo_merge(state,src,"https://fixture/",aliases,&scan,error);CHECK(next&&scan.added==2&&scan.memories==2);
    CHECK(!get(state,"gaboActivity"));CHECK(cJSON_GetArraySize(get(next,"gaboActivity"))==1);
    cJSON *entry=get(next,"gaboActivity")->child,*rows=get(entry,"models"),*a=cJSON_GetArrayItem(rows,0),*b=cJSON_GetArrayItem(rows,1);CHECK(activity_time_valid(str(entry,"at")));CHECK(cJSON_GetArraySize(rows)==2);
    CHECK(!strcmp(str(a,"model"),"alpha")&&get(a,"added")->valueint==1&&get(a,"messages")->valueint==2);CHECK(!strcmp(str(b,"model"),"beta")&&get(b,"added")->valueint==1&&get(b,"messages")->valueint==2);
    CHECK(cJSON_GetArraySize(get(a,"learned"))==1&&cJSON_GetArraySize(get(b,"learned"))==1);
    json=cJSON_PrintUnformatted(get(next,"gaboActivity"));CHECK(!strstr(json,"astronomy")&&!strstr(json,"short replies")&&!strstr(json,"Assistant-only"));free(json);json=NULL;
    cJSON_Delete(state);state=next;next=NULL;text=gabo_activity_text(state,check,"alpha",1,0);CHECK(strstr(text,"I like astronomy.")&&!strstr(text,"I prefer short replies."));CHECK(strstr(text,"Last successful check:")&&strstr(text,"Latest check succeeded.")&&strstr(text,"New memories learned: 1"));free(text);
    text=gabo_activity_text(state,check,"beta",1,1);CHECK(strstr(text,"I prefer short replies.")&&!strstr(text,"I like astronomy."));CHECK(strstr(text,"Checking Gabo net now"));free(text);text=NULL;
    next=gabo_merge(state,src,"https://fixture/",aliases,&scan,error);CHECK(next&&!scan.changed&&cJSON_Compare(get(next,"gaboActivity"),get(state,"gaboActivity"),1));cJSON_Delete(next);next=NULL;
    // A retry after a discarded/failed transaction still reports both additions.
    cJSON *empty=new_state();next=gabo_merge(empty,src,"https://fixture/",aliases,&scan,error);cJSON_Delete(empty);CHECK(next&&scan.memories==2&&cJSON_GetArraySize(get(next,"gaboActivity"))==1);cJSON_Delete(next);next=NULL;
    // Forgetting a fact removes its readable text from the activity view too.
    CHECK(set_memories(state,"",1,1,error));text=gabo_activity_text(state,check,"alpha",1,0);CHECK(!strstr(text,"astronomy")&&strstr(text,"later removed or edited"));free(text);text=NULL;
    json=cJSON_PrintUnformatted(state);next=import_state(json,NULL,error);free(json);json=NULL;CHECK(next&&cJSON_Compare(get(next,"gaboActivity"),get(state,"gaboActivity"),1));text=gabo_activity_text(next,check,"alpha",1,0);CHECK(!strstr(text,"astronomy"));free(text);text=NULL;cJSON_Delete(next);next=NULL;
    // A new turn containing an already saved fact imports messages, learns zero.
    cJSON *thread=cJSON_GetArrayItem(get(src,"threads"),1);cJSON_AddItemToArray(get(thread,"messages"),cJSON_Parse("{\"role\":\"user\",\"content\":\"I prefer short replies.\"}"));cJSON_AddItemToArray(get(thread,"messages"),cJSON_Parse("{\"role\":\"assistant\",\"content\":\"Again.\"}"));
    next=gabo_merge(state,src,"https://fixture/",aliases,&scan,error);CHECK(next&&scan.updated==1&&scan.memories==0);entry=get(next,"gaboActivity")->child;CHECK(cJSON_GetArraySize(get(entry,"models"))==1&&!strcmp(str(get(entry,"models")->child,"model"),"beta"));CHECK(!get(get(entry,"models")->child,"learned")->child);cJSON_Delete(state);state=next;next=NULL;
    CHECK(select_model(state,"beta",error));CHECK(set_memories(state,"I prefer short replies.",1,0,error));cJSON_AddItemToArray(get(thread,"messages"),cJSON_Parse("{\"role\":\"user\",\"content\":\"I use a microscope.\"}"));cJSON_AddItemToArray(get(thread,"messages"),cJSON_Parse("{\"role\":\"assistant\",\"content\":\"OK.\"}"));next=gabo_merge(state,src,"https://fixture/",aliases,&scan,error);CHECK(next&&scan.updated==1&&scan.memories==0&&!get(get(get(next,"gaboActivity")->child,"models")->child,"learned")->child);cJSON_Delete(state);state=next;next=NULL;
    // Unknown attribution cannot create a receipt or learn a fact.
    setstr(thread,"model","unknown");next=gabo_merge(state,src,"https://fixture/",aliases,&scan,error);CHECK(next&&scan.pending==3&&cJSON_Compare(get(next,"gaboActivity"),get(state,"gaboActivity"),1));cJSON_Delete(next);next=NULL;
    // Bounded retention keeps the newest 50 batches; backup validation rejects
    // malformed timestamps, model duplicates and non-digest memory payloads.
    int retained=1;entry=cJSON_Duplicate(get(state,"gaboActivity")->child,1);for(int i=0;i<60;i++){cJSON *receiptRows=cJSON_Duplicate(get(entry,"models"),1);retained&=record_gabo_activity(state,state,receiptRows,error);cJSON_Delete(receiptRows);}cJSON_Delete(entry);CHECK(retained&&cJSON_GetArraySize(get(state,"gaboActivity"))==50);
    json=cJSON_PrintUnformatted(state);next=import_state(json,NULL,error);free(json);json=NULL;CHECK(next);cJSON_Delete(next);next=NULL;
    entry=get(state,"gaboActivity")->child;setstr(entry,"at","2026-10-23T20:00:00X");json=cJSON_PrintUnformatted(state);next=import_state(json,NULL,error);free(json);json=NULL;CHECK(!next);setstr(entry,"at","2026-10-23T20:00:00Z");
    rows=get(entry,"models");cJSON_AddItemToArray(rows,cJSON_Duplicate(rows->child,1));json=cJSON_PrintUnformatted(state);next=import_state(json,NULL,error);free(json);json=NULL;CHECK(!next);cJSON_DeleteItemFromArray(rows,1);
    cJSON_AddItemToArray(get(rows->child,"learned"),cJSON_CreateString("Secret copied text"));json=cJSON_PrintUnformatted(state);next=import_state(json,NULL,error);free(json);json=NULL;CHECK(!next);cJSON_DeleteItemFromArray(get(rows->child,"learned"),0);
    cJSON_DeleteItemFromObjectCaseSensitive(state,"gaboActivity");text=gabo_activity_text(state,check,"alpha",0,0);CHECK(strstr(text,"Not recorded for older imports")&&strstr(text,"Automatic import: off"));free(text);text=NULL;
    cJSON_Delete(state);cJSON_Delete(src);cJSON_Delete(aliases);cJSON_Delete(check);return passed;
fail:free(text);free(json);cJSON_Delete(next);cJSON_Delete(state);cJSON_Delete(src);cJSON_Delete(aliases);cJSON_Delete(check);return -1;
}
