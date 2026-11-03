#define _CRT_SECURE_NO_WARNINGS
#include "spark.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REQUIRE(x) do { if(!(x)){fprintf(stderr,"FAIL backfill line %d: %s (%s)\n",__LINE__,#x,error);return -1;}checks++; }while(0)
static cJSON *active(cJSON *s){return find_id(get(s,"threads"),str(s,"activeThreadId"));}
static void message(cJSON *s,const char *role,const char *text){cJSON *m=cJSON_CreateObject();setstr(m,"role",role);setstr(m,"content",text);cJSON_AddItemToArray(get(active(s),"messages"),m);}
static int has(cJSON *s,const char *text){char *notes=memory_text(s);int result=strstr(notes,text)!=NULL;free(notes);return result;}
static int unchanged(cJSON *s,const char *before){char *after=cJSON_PrintUnformatted(s);int ok=!strcmp(before,after);free(after);return ok;}
static cJSON *reload(cJSON *s,char error[512]){char *json=cJSON_PrintUnformatted(s);cJSON *out=import_state(json,NULL,error);free(json);return out;}
int backfill_tests(void){
    int checks=0;char error[512]={0};MemoryScan scan;cJSON *s=new_state();
    REQUIRE(!learn_chat_memories(s,&scan,error));REQUIRE(select_model(s,"a:latest",error));
    REQUIRE(learn_chat_memories(s,&scan,error));REQUIRE(scan.added==0&&scan.messages==0&&scan.conversations==1);REQUIRE(!memory_notes(s));
    message(s,"user","My name is OLD_NAME.\nI prefer short answers.\nI'm learning C.");
    message(s,"assistant","My name is ASSISTANT_ONLY.\nI prefer ASSISTANT_PREFERENCE.");
    message(s,"system","I like SYSTEM_ONLY.");
    message(s,"user","> I like QUOTED.\n```\nI like CODE.\n```\n\"My name is QUOTED_NAME\"");
    message(s,"user","I live in OPT_OUT. Do not save this.");message(s,"user","I like ROLEPLAY. Let's pretend.");message(s,"user","I prefer QUESTIONS?");
    setstr(active(s),"draft","I like DRAFT_ONLY.");setstr(s,"persona","I like GLOBAL_PERSONA.");
    REQUIRE(set_memories(s,"My name is CURRENT_NAME.\nMANUAL_NOTE",0,0,error));
    char *history=cJSON_PrintUnformatted(get(s,"threads"));REQUIRE(learn_chat_memories(s,&scan,error));
    REQUIRE(scan.conversations==1&&scan.messages==5&&scan.added==2&&scan.conflicts==1);
    REQUIRE(has(s,"CURRENT_NAME")&&has(s,"MANUAL_NOTE")&&has(s,"short answers")&&has(s,"learning C"));
    REQUIRE(!has(s,"OLD_NAME")&&!has(s,"ASSISTANT")&&!has(s,"SYSTEM_ONLY")&&!has(s,"CODE")&&!has(s,"QUOTED")&&!has(s,"OPT_OUT")&&!has(s,"ROLEPLAY")&&!has(s,"QUESTIONS")&&!has(s,"DRAFT_ONLY")&&!has(s,"GLOBAL_PERSONA"));
    REQUIRE(!memory_option(s,"enabled")&&!memory_option(s,"automatic"));
    char *after=cJSON_PrintUnformatted(get(s,"threads"));REQUIRE(!strcmp(history,after));free(history);free(after);
    char *before=cJSON_PrintUnformatted(s);REQUIRE(learn_chat_memories(s,&scan,error));REQUIRE(scan.added==0&&unchanged(s,before));free(before);
    REQUIRE(select_model(s,"b:latest",error));message(s,"user","I like B_ONLY.");REQUIRE(learn_chat_memories(s,&scan,error));REQUIRE(scan.added==1&&scan.messages==1);REQUIRE(has(s,"B_ONLY")&&!has(s,"CURRENT_NAME"));
    REQUIRE(select_model(s,"a:other",error));message(s,"user","I like OTHER_TAG.");REQUIRE(learn_chat_memories(s,&scan,error));REQUIRE(has(s,"OTHER_TAG")&&!has(s,"B_ONLY")&&!has(s,"short answers"));
    REQUIRE(select_model(s,"a:latest",error));REQUIRE(has(s,"CURRENT_NAME")&&!has(s,"OTHER_TAG")&&!has(s,"B_ONLY"));
    // System roles are deliberately invalid in persisted history; the synthetic
    // role-filter probe above is removed before the ordinary import round trip.
    cJSON_DeleteItemFromArray(get(active(s),"messages"),2);
    cJSON *loaded=reload(s,error);REQUIRE(loaded!=NULL);REQUIRE(has(loaded,"short answers")&&!memory_option(loaded,"enabled"));cJSON_Delete(loaded);
    char *notes=memory_text(s);REQUIRE(set_memories(s,notes,1,0,error));free(notes);REQUIRE(create_conversation(s,error));message(s,"user","What do you remember?");
    cJSON *request=build_messages(s,active(s),error);REQUIRE(request!=NULL);char *json=cJSON_PrintUnformatted(request);REQUIRE(strstr(json,"short answers")&&strstr(json,"CURRENT_NAME")&&!strstr(json,"B_ONLY")&&!strstr(json,"OLD_NAME"));free(json);cJSON_Delete(request);cJSON_Delete(s);

    // Corrections in one transcript have known order. Conflicting facts across
    // transcripts have no reliable message timestamps, regardless of pin order.
    s=new_state();REQUIRE(select_model(s,"a",error));message(s,"user","My name is First.\nI live in Town.");message(s,"user","My name is Last.");
    REQUIRE(learn_chat_memories(s,&scan,error));REQUIRE(has(s,"Last")&&!has(s,"First"));REQUIRE(set_memories(s,"",1,1,error));
    REQUIRE(create_conversation(s,error));message(s,"user","My name is Other.\nI live in Town.");REQUIRE(learn_chat_memories(s,&scan,error));REQUIRE(scan.conflicts==2&&scan.added==1);REQUIRE(has(s,"Town")&&!has(s,"Last")&&!has(s,"Other"));
    REQUIRE(set_memories(s,"",1,1,error));cJSON *threads=get(s,"threads"),*first=cJSON_DetachItemFromArray(threads,0);cJSON_AddItemToArray(threads,first);
    REQUIRE(learn_chat_memories(s,&scan,error));REQUIRE(scan.conflicts==2&&scan.added==1&&has(s,"Town"));cJSON_Delete(s);

    // Forget commands affect proposed historical notes, never current notes.
    s=new_state();REQUIRE(select_model(s,"a",error));message(s,"user","I like FORGOTTEN.\nI like KEPT.");message(s,"user","/forget I like FORGOTTEN.");message(s,"user","/forget missing");
    REQUIRE(learn_chat_memories(s,&scan,error));REQUIRE(scan.added==1&&has(s,"KEPT")&&!has(s,"FORGOTTEN"));
    REQUIRE(set_memories(s,"CURRENT_NOTE",1,1,error));message(s,"user","/forget all");message(s,"user","I like AFTER_FORGET.");
    REQUIRE(create_conversation(s,error));message(s,"user","I like UNKNOWN_ORDER.");
    REQUIRE(learn_chat_memories(s,&scan,error));REQUIRE(has(s,"CURRENT_NOTE")&&has(s,"AFTER_FORGET")&&!has(s,"UNKNOWN_ORDER")&&!has(s,"KEPT"));REQUIRE(scan.skipped==1);cJSON_Delete(s);
    s=new_state();REQUIRE(select_model(s,"a",error));message(s,"user","I like CROSS_FORGOTTEN.");REQUIRE(create_conversation(s,error));message(s,"user","/forget I like CROSS_FORGOTTEN.");
    REQUIRE(learn_chat_memories(s,&scan,error));REQUIRE(scan.added==0&&scan.skipped==1);message(s,"user","I like CROSS_FORGOTTEN.");REQUIRE(learn_chat_memories(s,&scan,error));REQUIRE(scan.added==1&&has(s,"CROSS_FORGOTTEN"));cJSON_Delete(s);

    // Historical limits skip additional facts without evicting existing notes.
    s=new_state();REQUIRE(select_model(s,"a",error));REQUIRE(set_memories(s,"KEEP_ME",1,1,error));
    for(int i=0;i<40;i++){char text[64];snprintf(text,sizeof(text),"I like item %d.",i);message(s,"user",text);}
    REQUIRE(learn_chat_memories(s,&scan,error));REQUIRE(scan.added==31&&scan.skipped==9&&has(s,"KEEP_ME"));REQUIRE(cJSON_GetArraySize(memory_notes(s))==32);
    before=cJSON_PrintUnformatted(s);REQUIRE(learn_chat_memories(s,&scan,error));REQUIRE(scan.added==0&&unchanged(s,before));free(before);cJSON_Delete(s);
    s=new_state();REQUIRE(select_model(s,"a",error));char longnote[700];memset(longnote,'x',sizeof(longnote));memcpy(longnote,"Remember: ",10);longnote[sizeof(longnote)-1]=0;message(s,"user",longnote);message(s,"user","Remember: VALID_AFTER_LONG");
    REQUIRE(learn_chat_memories(s,&scan,error));REQUIRE(scan.skipped==1&&scan.added==1&&has(s,"VALID_AFTER_LONG"));
    REQUIRE(set_memories(s,"",1,1,error));
    for(int i=0;i<9;i++){char text[510];memset(text,'x',sizeof(text));memcpy(text,"I like ",7);text[7]=(char)('A'+i);text[sizeof(text)-1]=0;message(s,"user",text);}
    message(s,"user","I like SMALL_AFTER_FULL.");REQUIRE(learn_chat_memories(s,&scan,error));REQUIRE(scan.skipped>=2&&has(s,"SMALL_AFTER_FULL"));cJSON_Delete(s);

    loaded=import_state("{\"version\":3,\"selectedModel\":\"old\",\"threads\":[{\"id\":\"t\",\"model\":\"old\",\"messages\":[{\"role\":\"user\",\"content\":\"I prefer imported facts.\"}]}],\"activeThreadId\":\"t\",\"modelThreads\":{\"old\":\"t\"}}",NULL,error);
    REQUIRE(loaded!=NULL);REQUIRE(!memory_notes(loaded));REQUIRE(learn_chat_memories(loaded,&scan,error));REQUIRE(scan.added==1&&has(loaded,"imported facts"));cJSON_Delete(loaded);
    s=new_state();REQUIRE(select_model(s,"a",error));message(s,"user","I prefer NO_PARTIAL_COMMIT.");
    char *large=allocate(SPARK_LIMIT+1);memset(large,'x',SPARK_LIMIT);setstr(s,"persona",large);free(large);before=cJSON_PrintUnformatted(s);
    REQUIRE(!learn_chat_memories(s,&scan,error));REQUIRE(unchanged(s,before));free(before);cJSON_Delete(s);
    // Bound scan work even when one imported message has many candidate lines.
    s=new_state();REQUIRE(select_model(s,"a",error));Buffer many={0};for(int i=0;i<1000;i++){char line[40];snprintf(line,sizeof(line),"I like candidate %d.\n",i);adds(&many,line);}
    for(int i=0;i<9;i++)message(s,"user",many.p);free(many.p);before=cJSON_PrintUnformatted(s);
    REQUIRE(!learn_chat_memories(s,&scan,error));REQUIRE(strstr(error,"8,192")!=NULL&&unchanged(s,before));free(before);cJSON_Delete(s);
    return checks;
}
