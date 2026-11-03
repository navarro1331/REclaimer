#define _CRT_SECURE_NO_WARNINGS
#include "spark.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REQUIRE(x) do { if(!(x)){fprintf(stderr,"FAIL memory line %d: %s (%s)\n",__LINE__,#x,error);return -1;}checks++; }while(0)
static cJSON *reload(cJSON *state,char error[512]){char *text=cJSON_PrintUnformatted(state);cJSON *out=import_state(text,NULL,error);free(text);return out;}
static cJSON *active(cJSON *s){return find_id(get(s,"threads"),str(s,"activeThreadId"));}
static void user(cJSON *s,const char *text){cJSON *m=cJSON_CreateObject();setstr(m,"role","user");setstr(m,"content",text);cJSON_AddItemToArray(get(active(s),"messages"),m);}
static int has(cJSON *s,const char *text){char *notes=memory_text(s);int result=strstr(notes,text)!=NULL;free(notes);return result;}
static int unchanged(cJSON *s,const char *before){char *after=cJSON_PrintUnformatted(s);int ok=!strcmp(before,after);free(after);return ok;}
int memory_tests(void){
    int checks=0;char error[512]={0};cJSON *s=new_state();REQUIRE(get(s,"version")->valueint==5);REQUIRE(cJSON_IsObject(get(s,"modelMemories")));
    REQUIRE(remember_message(s,"My name is Unselected",error)==0);REQUIRE(!set_memories(s,"No model",1,1,error));
    REQUIRE(select_model(s,"a:latest",error));REQUIRE(memory_option(s,"enabled"));REQUIRE(memory_option(s,"automatic"));
    REQUIRE(remember_message(s,"My name is Ada. What is the weather?\nI prefer short answers.\nI'm learning C.",error)==1);
    REQUIRE(cJSON_GetArraySize(memory_notes(s))==3);REQUIRE(has(s,"My name is Ada."));REQUIRE(!has(s,"weather"));
    REQUIRE(remember_message(s,"My name is Ada.",error)==0);REQUIRE(cJSON_GetArraySize(memory_notes(s))==3);
    REQUIRE(remember_message(s,"My name is Bea.",error)==1);REQUIRE(cJSON_GetArraySize(memory_notes(s))==3);REQUIRE(!has(s,"Ada"));REQUIRE(has(s,"Bea"));
    REQUIRE(remember_message(s,"My pronouns are they/them.\nI live in Boston.",error)==1);REQUIRE(remember_message(s,"I live in Chicago.",error)==1);REQUIRE(!has(s,"Boston"));
    REQUIRE(remember_message(s,"Remember that my favorite constellation is Orion.",error)==1);REQUIRE(has(s,"constellation"));
    REQUIRE(remember_message(s,"> My name is Quoted\n```\nMy name is Code\n```\n\"My name is Example\"",error)==0);
    REQUIRE(remember_message(s,"My name is Fiction. Let's pretend.",error)==0);
    REQUIRE(remember_message(s,"I prefer blue?",error)==0);
    REQUIRE(remember_message(s,"My name is Temporary. Do not save this.",error)==0);
    REQUIRE(remember_message(s,"For this chat only: I prefer green.",error)==0);
    REQUIRE(remember_message(s,"I have a question about C.",error)==0);
    REQUIRE(create_conversation(s,error));REQUIRE(cJSON_GetArraySize(get(active(s),"messages"))==0);user(s,"What do you know about me?");
    cJSON *messages=build_messages(s,active(s),error);REQUIRE(messages!=NULL);char *text=cJSON_PrintUnformatted(messages);REQUIRE(strstr(text,"Bea")&&strstr(text,"short answers"));REQUIRE(!strstr(text,"Ada"));free(text);cJSON_Delete(messages);
    REQUIRE(select_model(s,"b:latest",error));REQUIRE(cJSON_GetArraySize(memory_notes(s))==0);user(s,"What do you know about me?");messages=build_messages(s,active(s),error);text=cJSON_PrintUnformatted(messages);REQUIRE(!strstr(text,"Bea")&&!strstr(text,"Orion"));free(text);cJSON_Delete(messages);
    REQUIRE(remember_message(s,"My name is B_ONLY",error)==1);REQUIRE(has(s,"B_ONLY"));
    REQUIRE(select_model(s,"a:latest",error));REQUIRE(has(s,"Bea"));REQUIRE(!has(s,"B_ONLY"));
    REQUIRE(select_model(s,"a:other",error));REQUIRE(cJSON_GetArraySize(memory_notes(s))==0);REQUIRE(select_model(s,"a:latest",error));
    cJSON *loaded=reload(s,error);REQUIRE(loaded!=NULL);REQUIRE(has(loaded,"Bea"));REQUIRE(select_model(loaded,"b:latest",error));REQUIRE(has(loaded,"B_ONLY"));cJSON_Delete(loaded);
    wchar_t temp[MAX_PATH],file[MAX_PATH],backup[MAX_PATH+8];GetTempPathW(MAX_PATH,temp);GetTempFileNameW(temp,L"gsm",0,file);DeleteFileW(file);
    REQUIRE(save_json(file,s,error));cJSON *raw=load_json(file,error);REQUIRE(raw!=NULL);loaded=reload(raw,error);REQUIRE(loaded!=NULL);REQUIRE(has(loaded,"Bea"));cJSON_Delete(raw);cJSON_Delete(loaded);DeleteFileW(file);swprintf(backup,MAX_PATH+8,L"%ls.bak",file);DeleteFileW(backup);
    REQUIRE(set_memories(s,"Manual note\r\n  I prefer tea.  \r\nManual note\r\n\r\nUnicode \xce\xa9",1,0,error));REQUIRE(cJSON_GetArraySize(memory_notes(s))==3);REQUIRE(!memory_option(s,"automatic"));
    REQUIRE(remember_message(s,"My name is NotAutomatic",error)==0);REQUIRE(remember_message(s,"/remember EXPLICIT_ONLY",error)==1);REQUIRE(has(s,"EXPLICIT_ONLY"));
    REQUIRE(remember_message(s,"/forget EXPLICIT_ONLY",error)==1);REQUIRE(!has(s,"EXPLICIT_ONLY"));REQUIRE(remember_message(s,"/forget missing",error)==-1);
    REQUIRE(set_memories(s,"DISABLED_NOTE",0,1,error));REQUIRE(!memory_option(s,"enabled"));REQUIRE(remember_message(s,"I prefer NotSaved",error)==0);REQUIRE(remember_message(s,"Remember: NotSaved",error)==-1);
    messages=build_messages(s,active(s),error);text=cJSON_PrintUnformatted(messages);REQUIRE(!strstr(text,"DISABLED_NOTE"));free(text);cJSON_Delete(messages);
    REQUIRE(remember_message(s,"/forget all",error)==1);REQUIRE(cJSON_GetArraySize(memory_notes(s))==0);REQUIRE(!memory_option(s,"enabled"));
    REQUIRE(select_model(s,"b:latest",error));REQUIRE(has(s,"B_ONLY"));REQUIRE(memory_option(s,"enabled"));
    REQUIRE(delete_conversation(s,str(s,"activeThreadId"),error));REQUIRE(has(s,"B_ONLY"));
    loaded=import_state("{\"name\":\"A character\"}",s,error);REQUIRE(loaded!=NULL);REQUIRE(has(loaded,"B_ONLY"));cJSON_Delete(loaded);
    loaded=reload(s,error);REQUIRE(loaded!=NULL);REQUIRE(select_model(loaded,"a:latest",error));REQUIRE(!memory_option(loaded,"enabled"));REQUIRE(cJSON_GetArraySize(memory_notes(loaded))==0);cJSON_Delete(loaded);
    text=cJSON_PrintUnformatted(s);char longnote[SPARK_MEMORY_TEXT+2];memset(longnote,'x',sizeof(longnote)-1);longnote[sizeof(longnote)-1]=0;
    REQUIRE(!set_memories(s,longnote,1,1,error));REQUIRE(unchanged(s,text));
    Buffer too_many={0};for(int i=0;i<33;i++){char line[32];snprintf(line,sizeof(line),"Note %d\n",i);adds(&too_many,line);}REQUIRE(!set_memories(s,too_many.p,1,1,error));REQUIRE(unchanged(s,text));free(too_many.p);
    Buffer too_large={0};longnote[500]=0;for(int i=0;i<9;i++){longnote[0]=(char)('a'+i);adds(&too_large,longnote);adds(&too_large,"\n");}REQUIRE(!set_memories(s,too_large.p,1,1,error));REQUIRE(unchanged(s,text));free(too_large.p);
    REQUIRE(!set_memories(s,"bad\xff",1,1,error));REQUIRE(unchanged(s,text));free(text);cJSON_Delete(s);
    // Old chats are retained but are never mined for unrequested memories.
    loaded=import_state("{\"version\":3,\"selectedModel\":\"old\",\"threads\":[{\"id\":\"t\",\"model\":\"old\",\"messages\":[{\"role\":\"user\",\"content\":\"My name is Old\"}]}],\"activeThreadId\":\"t\",\"modelThreads\":{\"old\":\"t\"}}",NULL,error);
    REQUIRE(loaded!=NULL);REQUIRE(get(loaded,"version")->valueint==5);REQUIRE(cJSON_GetArraySize(get(loaded,"modelMemories"))==0);REQUIRE(!strcmp(str(loaded,"activeThreadId"),"t"));cJSON_Delete(loaded);
    REQUIRE(import_state("{\"version\":4,\"threads\":[],\"modelThreads\":{}}",NULL,error)==NULL);
    REQUIRE(import_state("{\"threads\":[],\"modelMemories\":[]}",NULL,error)==NULL);
    REQUIRE(import_state("{\"threads\":[],\"modelMemories\":{\"\":{\"enabled\":true,\"automatic\":true,\"notes\":[]}}}",NULL,error)==NULL);
    REQUIRE(import_state("{\"threads\":[],\"modelMemories\":{\"x\":{\"enabled\":1,\"automatic\":true,\"notes\":[]}}}",NULL,error)==NULL);
    REQUIRE(import_state("{\"threads\":[],\"modelMemories\":{\"x\":{\"enabled\":true,\"automatic\":true,\"notes\":[{}]}}}",NULL,error)==NULL);
    REQUIRE(import_state("{\"threads\":[],\"modelMemories\":{\"x\":{\"enabled\":true,\"automatic\":true,\"notes\":[\"bad\\nline\"]}}}",NULL,error)==NULL);
    REQUIRE(import_state("{\"threads\":[],\"modelMemories\":{\"x\":{\"enabled\":true,\"automatic\":true,\"notes\":[\"\"]}}}",NULL,error)==NULL);
    s=new_state();REQUIRE(select_model(s,"last-model",error));cJSON *map=get(s,"modelMemories");
    for(int i=0;i<512;i++){char key[32];snprintf(key,sizeof(key),"saved-%d",i);cJSON *p=cJSON_CreateObject();cJSON_AddBoolToObject(p,"enabled",1);cJSON_AddBoolToObject(p,"automatic",1);cJSON_AddArrayToObject(p,"notes");cJSON_AddItemToObject(map,key,p);}
    text=cJSON_PrintUnformatted(s);REQUIRE(!set_memories(s,"Too many models",1,1,error));REQUIRE(unchanged(s,text));free(text);REQUIRE(select_model(s,"saved-0",error));REQUIRE(set_memories(s,"Existing model still editable",1,1,error));
    cJSON_AddItemToObject(map,"extra",cJSON_Duplicate(map->child,1));REQUIRE(reload(s,error)==NULL);cJSON_Delete(s);
    return checks;
}
