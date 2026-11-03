#define _CRT_SECURE_NO_WARNINGS
#include "spark.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REQUIRE(x) do { if(!(x)){fprintf(stderr,"FAIL history line %d: %s (%s)\n",__LINE__,#x,error);return -1;}checks++; }while(0)
static cJSON *active(cJSON *s){return find_id(get(s,"threads"),str(s,"activeThreadId"));}
static void message(cJSON *t,const char *role,const char *text){cJSON *m=cJSON_CreateObject();setstr(m,"role",role);setstr(m,"content",text);cJSON_AddItemToArray(get(t,"messages"),m);}
static cJSON *reload(cJSON *s,char error[512]){char *text=cJSON_PrintUnformatted(s);cJSON *out=import_state(text,NULL,error);free(text);return out;}
static int unchanged(cJSON *s,const char *before){char *after=cJSON_PrintUnformatted(s);int same=!strcmp(before,after);free(after);return same;}
int history_tests(void){
    int checks=0;char error[512]={0};cJSON *s=new_state();REQUIRE(get(s,"version")->valueint==5);REQUIRE(cJSON_IsObject(get(s,"modelThreads")));
    message(active(s),"user","BEFORE_MODEL");char a[128];strcpy(a,str(s,"activeThreadId"));
    REQUIRE(select_model(s,"gemma3:latest",error));REQUIRE(!strcmp(str(s,"activeThreadId"),a));REQUIRE(!strcmp(str(active(s),"model"),"gemma3:latest"));
    message(active(s),"assistant","GEMMA_REPLY");setstr(active(s),"draft","Gemma draft \xce\xa9");setstr(active(s),"context","GEMMA_CONTEXT");
    REQUIRE(select_model(s,"llama3:latest",error));char b[128];strcpy(b,str(s,"activeThreadId"));REQUIRE(strcmp(a,b));REQUIRE(cJSON_GetArraySize(get(active(s),"messages"))==0);REQUIRE(!*str(active(s),"draft"));
    message(active(s),"user","LLAMA_HISTORY");message(active(s),"assistant","LLAMA_REPLY");setstr(active(s),"draft","Llama draft");
    REQUIRE(select_model(s,"gemma3:latest",error));REQUIRE(!strcmp(str(s,"activeThreadId"),a));REQUIRE(!strcmp(str(active(s),"draft"),"Gemma draft \xce\xa9"));REQUIRE(cJSON_GetArraySize(get(active(s),"messages"))==2);
    message(active(s),"user","Continue GEMMA");cJSON *messages=build_messages(s,active(s),error);REQUIRE(messages!=NULL);char *text=cJSON_PrintUnformatted(messages);REQUIRE(strstr(text,"GEMMA_REPLY")&&strstr(text,"GEMMA_CONTEXT"));REQUIRE(!strstr(text,"LLAMA_HISTORY"));free(text);cJSON_Delete(messages);
    REQUIRE(build_messages(s,find_id(get(s,"threads"),b),error)==NULL);REQUIRE(!select_thread(s,b,error));REQUIRE(!strcmp(str(s,"activeThreadId"),a));
    REQUIRE(create_conversation(s,error));char a2[128];strcpy(a2,str(s,"activeThreadId"));REQUIRE(strcmp(a,a2));message(active(s),"user","SECOND_GEMMA");
    REQUIRE(select_model(s,"llama3:latest",error));REQUIRE(!strcmp(str(s,"activeThreadId"),b));REQUIRE(select_model(s,"gemma3:latest",error));REQUIRE(!strcmp(str(s,"activeThreadId"),a2));
    REQUIRE(select_thread(s,a,error));REQUIRE(select_model(s,"llama3:latest",error));REQUIRE(select_model(s,"gemma3:latest",error));REQUIRE(!strcmp(str(s,"activeThreadId"),a));
    cJSON *loaded=reload(s,error);REQUIRE(loaded!=NULL);REQUIRE(!strcmp(str(loaded,"activeThreadId"),a));REQUIRE(!strcmp(str(active(loaded),"draft"),"Gemma draft \xce\xa9"));
    REQUIRE(select_model(loaded,"llama3:latest",error));REQUIRE(!strcmp(str(loaded,"activeThreadId"),b));REQUIRE(!strcmp(str(active(loaded),"draft"),"Llama draft"));cJSON_Delete(loaded);
    wchar_t temp[MAX_PATH],file[MAX_PATH],backup[MAX_PATH+8];GetTempPathW(MAX_PATH,temp);GetTempFileNameW(temp,L"gsh",0,file);DeleteFileW(file);
    REQUIRE(save_json(file,s,error));cJSON *raw=load_json(file,error);REQUIRE(raw!=NULL);loaded=reload(raw,error);REQUIRE(loaded!=NULL);REQUIRE(select_model(loaded,"llama3:latest",error));REQUIRE(!strcmp(str(loaded,"activeThreadId"),b));cJSON_Delete(raw);cJSON_Delete(loaded);DeleteFileW(file);swprintf(backup,MAX_PATH+8,L"%ls.bak",file);DeleteFileW(backup);
    REQUIRE(!delete_conversation(s,b,error));REQUIRE(delete_conversation(s,a2,error));REQUIRE(!strcmp(str(s,"activeThreadId"),a));REQUIRE(!find_id(get(s,"threads"),a2));
    REQUIRE(delete_conversation(s,str(s,"activeThreadId"),error));REQUIRE(strcmp(str(s,"activeThreadId"),a));REQUIRE(!strcmp(str(active(s),"model"),"gemma3:latest"));REQUIRE(cJSON_GetArraySize(get(active(s),"messages"))==0);
    REQUIRE(select_model(s,"llama3:latest",error));REQUIRE(!strcmp(str(s,"activeThreadId"),b));REQUIRE(cJSON_GetArraySize(get(active(s),"messages"))==2);
    REQUIRE(select_model(s,"llama3:other-tag",error));REQUIRE(strcmp(str(s,"activeThreadId"),b));REQUIRE(cJSON_GetArraySize(get(active(s),"messages"))==0);
    // The remembered conversation wins over an inconsistent legacy UI cache.
    REQUIRE(select_model(s,"llama3:latest",error));setstr(s,"activeThreadId",a);loaded=reload(s,error);REQUIRE(loaded!=NULL);REQUIRE(!strcmp(str(loaded,"activeThreadId"),b));cJSON_Delete(loaded);
    setstr(get(s,"modelThreads"),"llama3:latest","deleted");loaded=reload(s,error);REQUIRE(loaded!=NULL);REQUIRE(!strcmp(str(loaded,"activeThreadId"),b));cJSON_Delete(loaded);
    setstr(get(s,"modelThreads"),"llama3:latest",str(get(s,"modelThreads"),"gemma3:latest"));REQUIRE(reload(s,error)==NULL);cJSON_Delete(s);
    const char *legacy="{\"version\":2,\"selectedModel\":\"old:tag\",\"activeThreadId\":\"second\",\"modelCharacters\":{\"old:tag\":\"c\"},\"characters\":[{\"id\":\"c\",\"name\":\"Guide\"}],\"threads\":[{\"id\":\"first\",\"name\":\"First\",\"pinned\":true,\"context\":\"Lore\",\"messages\":[{\"role\":\"user\",\"content\":\"KEEP_ONE\"}]},{\"id\":\"second\",\"name\":\"Second\",\"messages\":[{\"role\":\"assistant\",\"content\":\"KEEP_TWO\"}]}]}";
    s=import_state(legacy,NULL,error);REQUIRE(s!=NULL);REQUIRE(get(s,"version")->valueint==5);REQUIRE(!strcmp(str(s,"activeThreadId"),"second"));REQUIRE(!strcmp(str(s,"activeCharacterId"),"c"));
    REQUIRE(!strcmp(str(find_id(get(s,"threads"),"first"),"model"),"old:tag"));REQUIRE(cJSON_IsTrue(get(find_id(get(s,"threads"),"first"),"pinned")));REQUIRE(!strcmp(str(find_id(get(s,"threads"),"first"),"context"),"Lore"));
    REQUIRE(select_model(s,"new:tag",error));REQUIRE(cJSON_GetArraySize(get(active(s),"messages"))==0);REQUIRE(select_model(s,"old:tag",error));REQUIRE(!strcmp(str(s,"activeThreadId"),"second"));REQUIRE(!strcmp(str(get(active(s),"messages")->child,"content"),"KEEP_TWO"));
    loaded=import_state("{\"name\":\"New character\"}",s,error);REQUIRE(loaded!=NULL);REQUIRE(!strcmp(str(loaded,"activeThreadId"),"second"));REQUIRE(cJSON_GetArraySize(get(loaded,"threads"))==3);cJSON_Delete(loaded);cJSON_Delete(s);
    REQUIRE(import_state("{\"version\":3,\"threads\":[{\"model\":5}],\"modelThreads\":{}}",NULL,error)==NULL);
    REQUIRE(import_state("{\"version\":3,\"threads\":[{}],\"modelThreads\":{}}",NULL,error)==NULL);
    REQUIRE(import_state("{\"version\":3,\"threads\":[],\"modelThreads\":[]}",NULL,error)==NULL);
    REQUIRE(import_state("{\"version\":3,\"threads\":[],\"modelThreads\":{\"\":\"x\"}}",NULL,error)==NULL);
    REQUIRE(import_state("{\"version\":3,\"threads\":[],\"modelThreads\":{\"model\":5}}",NULL,error)==NULL);
    REQUIRE(import_state("{\"version\":3,\"threads\":[{\"model\":\"x\",\"draft\":5}],\"modelThreads\":{}}",NULL,error)==NULL);
    s=new_state();REQUIRE(select_model(s,"one",error));cJSON *card=cJSON_CreateObject();setstr(card,"id","c");setstr(card,"name","Guide");setstr(card,"greeting","HELLO_TWO");cJSON_AddItemToArray(get(s,"characters"),card);setstr(get(s,"modelCharacters"),"two","c");
    REQUIRE(select_model(s,"two",error));REQUIRE(!strcmp(str(get(active(s),"messages")->child,"content"),"HELLO_TWO"));REQUIRE(select_model(s,"one",error));REQUIRE(select_model(s,"two",error));REQUIRE(cJSON_GetArraySize(get(active(s),"messages"))==1);
    REQUIRE(create_conversation(s,error));REQUIRE(!strcmp(str(get(active(s),"messages")->child,"content"),"HELLO_TWO"));
    while(cJSON_GetArraySize(get(s,"threads"))<100){cJSON *t=new_thread("Limit fixture");setstr(t,"model","one");cJSON_AddItemToArray(get(s,"threads"),t);}
    text=cJSON_PrintUnformatted(s);REQUIRE(!select_model(s,"third",error));REQUIRE(unchanged(s,text));REQUIRE(!create_conversation(s,error));REQUIRE(unchanged(s,text));free(text);REQUIRE(select_model(s,"one",error));
    REQUIRE(delete_conversation(s,str(s,"activeThreadId"),error));REQUIRE(cJSON_GetArraySize(get(s,"threads"))==99);REQUIRE(select_model(s,"third",error));REQUIRE(cJSON_GetArraySize(get(s,"threads"))==100);cJSON_Delete(s);
    return checks;
}

