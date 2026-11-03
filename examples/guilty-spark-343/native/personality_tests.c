#define _CRT_SECURE_NO_WARNINGS
#include "spark.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REQUIRE(x) do { if(!(x)){fprintf(stderr,"FAIL personality line %d: %s\n",__LINE__,#x);return -1;}checks++; }while(0)
static void add_character(cJSON *state,const char *id,const char *name,const char *prompt){cJSON *c=cJSON_CreateObject();setstr(c,"id",id);setstr(c,"name",name);setstr(c,"systemPrompt",prompt);cJSON_AddItemToArray(get(state,"characters"),c);}
static cJSON *roundtrip(cJSON *state,char error[512]){char *json=cJSON_PrintUnformatted(state);cJSON *loaded=import_state(json,NULL,error);free(json);return loaded;}
int personality_tests(void){
    int checks=0;char error[512]={0};cJSON *s=new_state();REQUIRE(get(s,"version")->valueint==5);REQUIRE(cJSON_IsObject(get(s,"modelCharacters")));
    add_character(s,"guide","Patient guide","MODEL_A_ONLY: explain patiently.");add_character(s,"coder","Code coach","MODEL_B_ONLY: review the code.");
    REQUIRE(select_model(s,"gemma3:latest",error));REQUIRE(!*str(s,"activeCharacterId"));REQUIRE(select_character(s,"guide",error));
    REQUIRE(select_model(s,"llama3:latest",error));REQUIRE(!*str(s,"activeCharacterId"));REQUIRE(select_character(s,"coder",error));
    REQUIRE(select_model(s,"gemma3:latest",error));REQUIRE(!strcmp(str(s,"activeCharacterId"),"guide"));
    REQUIRE(select_model(s,"llama3:latest",error));REQUIRE(!strcmp(str(s,"activeCharacterId"),"coder"));
    REQUIRE(select_model(s,"llama3:other-tag",error));REQUIRE(!*str(s,"activeCharacterId"));
    REQUIRE(select_model(s,"gemma3:latest",error));REQUIRE(select_character(s,"",error));REQUIRE(!get(get(s,"modelCharacters"),"gemma3:latest"));
    REQUIRE(select_model(s,"llama3:latest",error));REQUIRE(!strcmp(str(s,"activeCharacterId"),"coder"));REQUIRE(select_model(s,"gemma3:latest",error));REQUIRE(!*str(s,"activeCharacterId"));
    REQUIRE(!select_character(s,"missing-character",error));REQUIRE(!*str(s,"activeCharacterId"));REQUIRE(select_character(s,"guide",error));
    REQUIRE(select_model(s,str(s,"selectedModel"),error));REQUIRE(select_character(s,str(s,"activeCharacterId"),error));
    cJSON *t=find_id(get(s,"threads"),str(s,"activeThreadId")),*user=cJSON_CreateObject();setstr(user,"role","user");setstr(user,"content","Keep this conversation intact.");cJSON_AddItemToArray(get(t,"messages"),user);
    // The request must use the selected model's saved character even if the
    // cached UI selection is stale. No personality crosses into another model.
    setstr(s,"activeCharacterId","coder");cJSON *messages=build_messages(s,t,error);REQUIRE(messages!=NULL);REQUIRE(strstr(str(messages->child,"content"),"MODEL_A_ONLY")!=NULL);REQUIRE(strstr(str(messages->child,"content"),"MODEL_B_ONLY")==NULL);cJSON_Delete(messages);
    REQUIRE(select_model(s,"llama3:latest",error));t=find_id(get(s,"threads"),str(s,"activeThreadId"));user=cJSON_CreateObject();setstr(user,"role","user");setstr(user,"content","A separate conversation.");cJSON_AddItemToArray(get(t,"messages"),user);messages=build_messages(s,t,error);REQUIRE(strstr(str(messages->child,"content"),"MODEL_B_ONLY")!=NULL);REQUIRE(strstr(str(messages->child,"content"),"MODEL_A_ONLY")==NULL);cJSON_Delete(messages);
    cJSON *loaded=roundtrip(s,error);REQUIRE(loaded!=NULL);REQUIRE(!strcmp(str(loaded,"activeCharacterId"),"coder"));REQUIRE(select_model(loaded,"gemma3:latest",error));REQUIRE(!strcmp(str(loaded,"activeCharacterId"),"guide"));REQUIRE(cJSON_GetArraySize(get(find_id(get(loaded,"threads"),str(loaded,"activeThreadId")),"messages"))==1);cJSON_Delete(loaded);
    wchar_t temp[MAX_PATH],file[MAX_PATH],backup[MAX_PATH+8];GetTempPathW(MAX_PATH,temp);GetTempFileNameW(temp,L"gsp",0,file);DeleteFileW(file);REQUIRE(save_json(file,s,error));cJSON *raw=load_json(file,error);REQUIRE(raw!=NULL);loaded=roundtrip(raw,error);REQUIRE(loaded!=NULL);REQUIRE(!strcmp(str(get(loaded,"modelCharacters"),"gemma3:latest"),"guide"));REQUIRE(!strcmp(str(loaded,"activeCharacterId"),"coder"));cJSON_Delete(raw);cJSON_Delete(loaded);DeleteFileW(file);swprintf(backup,MAX_PATH+8,L"%ls.bak",file);DeleteFileW(backup);
    setstr(find_id(get(s,"characters"),"coder"),"name","Renamed code coach");REQUIRE(select_model(s,"llama3:latest",error));REQUIRE(!strcmp(str(s,"activeCharacterId"),"coder"));
    delete_character(s,"guide");REQUIRE(!get(get(s,"modelCharacters"),"gemma3:latest"));REQUIRE(!strcmp(str(s,"activeCharacterId"),"coder"));REQUIRE(select_model(s,"gemma3:latest",error));REQUIRE(!*str(s,"activeCharacterId"));
    loaded=import_state("{\"name\":\"Imported guide\",\"personality\":\"Calm\"}",s,error);REQUIRE(loaded!=NULL);REQUIRE(!strcmp(str(get(loaded,"modelCharacters"),"gemma3:latest"),str(loaded,"activeCharacterId")));REQUIRE(!strcmp(str(get(loaded,"modelCharacters"),"llama3:latest"),"coder"));cJSON_Delete(loaded);
    const char *legacy="{\"version\":1,\"threads\":[],\"characters\":[{\"id\":\"c\",\"name\":\"Existing character\"}],\"activeCharacterId\":\"c\",\"selectedModel\":\"old-model:tag\"}";
    loaded=import_state(legacy,NULL,error);REQUIRE(loaded!=NULL);REQUIRE(get(loaded,"version")->valueint==5);REQUIRE(!strcmp(str(get(loaded,"modelCharacters"),"old-model:tag"),"c"));REQUIRE(select_model(loaded,"new-model:tag",error));REQUIRE(!*str(loaded,"activeCharacterId"));REQUIRE(select_model(loaded,"old-model:tag",error));REQUIRE(!strcmp(str(loaded,"activeCharacterId"),"c"));cJSON_Delete(loaded);
    loaded=import_state("{\"characters\":[{\"id\":\"c\",\"name\":\"Pending\"}],\"activeCharacterId\":\"c\"}",NULL,error);REQUIRE(loaded!=NULL);REQUIRE(select_model(loaded,"",error));REQUIRE(!strcmp(str(loaded,"activeCharacterId"),"c"));REQUIRE(select_model(loaded,"first-model",error));REQUIRE(!strcmp(str(get(loaded,"modelCharacters"),"first-model"),"c"));REQUIRE(select_model(loaded,"second-model",error));REQUIRE(!*str(loaded,"activeCharacterId"));cJSON_Delete(loaded);
    loaded=import_state("{\"version\":2,\"characters\":[{\"id\":\"c\",\"name\":\"Existing\"}],\"selectedModel\":\"model\",\"activeCharacterId\":\"c\",\"modelCharacters\":{\"model\":\"deleted\"}}",NULL,error);REQUIRE(loaded!=NULL);REQUIRE(!*str(loaded,"activeCharacterId"));REQUIRE(cJSON_GetArraySize(get(loaded,"modelCharacters"))==0);cJSON_Delete(loaded);
    REQUIRE(import_state("{\"threads\":[],\"modelCharacters\":[]}",NULL,error)==NULL);
    REQUIRE(import_state("{\"threads\":[],\"modelCharacters\":{\"model\":{}}}",NULL,error)==NULL);
    REQUIRE(import_state("{\"threads\":[],\"modelCharacters\":{\"\":\"c\"}}",NULL,error)==NULL);
    REQUIRE(import_state("{\"threads\":[],\"modelCharacters\":{\"model\":\"c\",\"model\":\"d\"}}",NULL,error)==NULL);
    cJSON *map=get(s,"modelCharacters");cJSON_ReplaceItemInObjectCaseSensitive(s,"modelCharacters",cJSON_CreateObject());map=get(s,"modelCharacters");for(int i=0;i<SPARK_MODEL_CHARACTERS;i++){char name[32];snprintf(name,sizeof(name),"model-%d",i);setstr(map,name,"coder");}
    REQUIRE(select_model(s,"new",error));REQUIRE(!select_character(s,"coder",error));REQUIRE(!get(map,"new"));REQUIRE(!*str(s,"activeCharacterId"));
    setstr(map,"too-many","coder");REQUIRE(roundtrip(s,error)==NULL);cJSON_DeleteItemFromObjectCaseSensitive(map,"too-many");
    REQUIRE(select_model(s,"model-0",error));REQUIRE(select_character(s,"",error));REQUIRE(select_model(s,"new",error));REQUIRE(select_character(s,"coder",error));REQUIRE(cJSON_GetArraySize(map)==SPARK_MODEL_CHARACTERS);
    delete_character(s,"coder");REQUIRE(!*str(s,"activeCharacterId"));REQUIRE(cJSON_GetArraySize(map)==0);cJSON_Delete(s);return checks;
}

