#define _CRT_SECURE_NO_WARNINGS
#include "spark.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static cJSON *profile(const cJSON *state){return get(get(state,"modelMemories"),str(state,"selectedModel"));}
int memory_option(const cJSON *state,const char *option){return !cJSON_IsFalse(get(profile(state),option));}
cJSON *memory_notes(const cJSON *state){return get(profile(state),"notes");}
char *memory_text(const cJSON *state){Buffer b={0};cJSON *n;cJSON_ArrayForEach(n,memory_notes(state)){if(b.n)adds(&b,"\r\n");adds(&b,n->valuestring);}return b.p?b.p:_strdup("");}
static char *trim(const char *text,size_t length){while(length&&isspace((unsigned char)*text)){text++;length--;}while(length&&isspace((unsigned char)text[length-1]))length--;char *s=allocate(length+1);memcpy(s,text,length);return s;}
static int prefix(const char *text,const char *p){return !_strnicmp(text,p,strlen(p));}
static int contains(const char *text,const char *part){for(;*text;text++)if(prefix(text,part))return 1;return 0;}
static int valid_note(const char *text,char err[512]){
    if(!*text||strlen(text)>SPARK_MEMORY_TEXT||strchr(text,'\n')||strchr(text,'\r')||!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text,-1,NULL,0)){strcpy(err,"Each memory must be one nonempty line, at most 512 UTF-8 bytes.");return 0;}
    for(const unsigned char *p=(const unsigned char*)text;*p;p++)if(*p<32&&*p!='\t'){strcpy(err,"Memory contains unsupported control characters.");return 0;}return 1;
}
static int bounded(const cJSON *notes,char err[512]){
    size_t bytes=0;cJSON *n;if(!cJSON_IsArray(notes)||cJSON_GetArraySize(notes)>SPARK_MEMORY_ITEMS){strcpy(err,"A model can keep up to 32 memories. Edit or delete an older memory first.");return 0;}
    cJSON_ArrayForEach(n,notes){if(!cJSON_IsString(n)){strcpy(err,"Saved memories must be text strings.");return 0;}if(!valid_note(n->valuestring,err))return 0;bytes+=strlen(n->valuestring)+1;}
    if(bytes>SPARK_MEMORY_TOTAL){strcpy(err,"This model's memories exceed 4,096 UTF-8 bytes. Shorten or delete an older memory first.");return 0;}return 1;
}
static cJSON *make_profile(cJSON *notes,int enabled,int automatic){cJSON *p=cJSON_CreateObject();cJSON_AddBoolToObject(p,"enabled",enabled);cJSON_AddBoolToObject(p,"automatic",automatic);cJSON_AddItemToObject(p,"notes",notes);return p;}
static unsigned nodes(const cJSON *value){if(!value)return 0;unsigned count=1;cJSON *v;cJSON_ArrayForEach(v,value)count+=nodes(v);return count;}
static int commit(cJSON *state,cJSON *notes,int enabled,int automatic,char err[512]){
    const char *model=str(state,"selectedModel");cJSON *map=get(state,"modelMemories");
    if(!*model||!cJSON_IsObject(map)){strcpy(err,"Select a model before saving memories.");cJSON_Delete(notes);return 0;}
    if(!get(map,model)&&cJSON_GetArraySize(map)>=512){strcpy(err,"Memory settings are already stored for 512 models.");cJSON_Delete(notes);return 0;}
    if(!bounded(notes,err)){cJSON_Delete(notes);return 0;}
    cJSON *p=make_profile(notes,enabled,automatic),*old=get(map,model);char *all=cJSON_PrintUnformatted(state),*before=old?cJSON_PrintUnformatted(old):NULL,*after=cJSON_PrintUnformatted(p);
    // Calculate the exact resulting JSON size, including the escaped model key.
    cJSON *one=cJSON_CreateObject();cJSON_AddItemReferenceToObject(one,model,p);char *entry=cJSON_PrintUnformatted(one);
    size_t size=strlen(all)-(before?strlen(before):0)+(before?strlen(after):strlen(entry)-2+(map->child?1:0));
    free(all);free(before);free(after);free(entry);cJSON_Delete(one);
    if(size>SPARK_LIMIT||nodes(state)-nodes(old)+nodes(p)>30000){cJSON_Delete(p);strcpy(err,"Workspace is full. Export a backup and remove older content before saving memories.");return 0;}
    if(old)cJSON_ReplaceItemInObjectCaseSensitive(map,model,p);else cJSON_AddItemToObject(map,model,p);return 1;
}
int set_memories(cJSON *state,const char *text,int enabled,int automatic,char err[512]){
    err[0]=0;cJSON *notes=cJSON_CreateArray();const char *p=text;
    while(*p){const char *end=strchr(p,'\n');if(!end)end=p+strlen(p);char *line=trim(p,(size_t)(end-p));
        if(*line){int duplicate=0;cJSON *n;cJSON_ArrayForEach(n,notes)if(!_stricmp(n->valuestring,line))duplicate=1;if(!duplicate)cJSON_AddItemToArray(notes,cJSON_CreateString(line));}free(line);p=*end?end+1:end;
    }return commit(state,notes,enabled,automatic,err);
}
int import_memories(cJSON *state,const cJSON *source,int required,char err[512]){
    cJSON *src=get(source,"modelMemories"),*map=get(state,"modelMemories"),*p;if(!src&&!required)return 1;
    if(!cJSON_IsObject(src)||cJSON_GetArraySize(src)>512){strcpy(err,"Model memories must be an object with at most 512 entries.");return 0;}
    cJSON_ArrayForEach(p,src){
        if(!p->string||!*p->string||strlen(p->string)>SPARK_TEXT||!cJSON_IsObject(p)||!cJSON_IsBool(get(p,"enabled"))||!cJSON_IsBool(get(p,"automatic"))){strcpy(err,"Each model memory needs a model name and enabled/automatic boolean settings.");return 0;}
        if(!bounded(get(p,"notes"),err))return 0;
        cJSON_AddItemToObject(map,p->string,make_profile(cJSON_Duplicate(get(p,"notes"),1),cJSON_IsTrue(get(p,"enabled")),cJSON_IsTrue(get(p,"automatic"))));
    }return 1;
}
static int slot(const char *text){
    if(prefix(text,"My name is ")||prefix(text,"Call me "))return 1;
    if(prefix(text,"My pronouns are "))return 2;
    if(prefix(text,"I live in "))return 3;
    return 0;
}
static int fact(const char *text){
    const char *starts[]={"My name is ","Call me ","My pronouns are ","I live in ","I prefer ","I like ","I dislike ","I work as ","I work in ","I work at ","I am learning ","I'm learning ","My goal is ","I use "};
    for(unsigned i=0;i<sizeof(starts)/sizeof(starts[0]);i++)if(prefix(text,starts[i])&&text[strlen(starts[i])])return 1;return 0;
}
static int add_fact(cJSON *notes,const char *text,char err[512]){
    if(!valid_note(text,err))return -1;cJSON *n;int key=slot(text);
    cJSON_ArrayForEach(n,notes){if(!_stricmp(n->valuestring,text))return 0;if(key&&slot(n->valuestring)==key){cJSON_SetValuestring(n,text);return 1;}}
    cJSON_AddItemToArray(notes,cJSON_CreateString(text));return 1;
}
typedef struct { cJSON *notes,*forgotten; int enabled,automatic,changed,skipped; int *remaining; } Capture;
static int capture_budget(Capture *capture,char err[512]){
    if(capture->remaining&&--*capture->remaining<0){strcpy(err,"This model's chats contain more than 8,192 candidate facts or forget commands. No notes were changed; use the memory editor to select important facts manually.");return 0;}return 1;
}
// Both callers supply only user messages. A historical capture has a separate
// forget list and tolerates stale commands and oversized facts in old chats.
static int capture_message(Capture *capture,const char *text,char err[512]){
    if(contains(text,"don't remember")||contains(text,"do not remember")||contains(text,"don't save")||contains(text,"do not save")||contains(text,"this chat only")||contains(text,"pretend")||contains(text,"roleplay")||contains(text,"role-play"))return 1;
    int code=0;
    const char *p=text;while(*p){const char *end=strchr(p,'\n');if(!end)end=p+strlen(p);char *line=trim(p,(size_t)(end-p));
        if(prefix(line,"```")){code=!code;free(line);p=*end?end+1:end;continue;}
        if(!code&&*line&&*line!='>'&&*line!='"'&&*line!='\''){
            const char *value=line;int explicit_save=0;
            const char *commands[]={"/remember ","Remember that ","Remember: "};
            for(unsigned i=0;i<3;i++)if(prefix(line,commands[i])){value=line+strlen(commands[i]);explicit_save=1;break;}
            if(prefix(line,"/forget ")){
                if(!capture_budget(capture,err)){free(line);return 0;}
                value=line+8;int found=0;if(!_stricmp(value,"all")){found=cJSON_GetArraySize(capture->notes)>0;cJSON_Delete(capture->notes);capture->notes=cJSON_CreateArray();}
                else {cJSON *n=capture->notes->child;while(n){cJSON *next=n->next;if(!_stricmp(n->valuestring,value)){cJSON_Delete(cJSON_DetachItemViaPointer(capture->notes,n));found=1;}n=next;}}
                if(capture->forgotten)cJSON_AddItemToArray(capture->forgotten,cJSON_CreateString(value));
                else if(!found){strcpy(err,"No matching saved memory. Open Memories to review or remove it.");free(line);return 0;}capture->changed|=found;
            }else if(explicit_save||capture->automatic){
                if(!capture->enabled){if(explicit_save){strcpy(err,"Memories are turned off for this model. Enable them in Memories to save this fact.");free(line);return 0;}}
                else if(explicit_save||fact(value)){
                    // Automatic capture uses one clear declarative sentence per
                    // line; questions and longer paragraphs are left for review.
                    char *candidate=_strdup(value);
                    if(!explicit_save){for(char *q=candidate;*q;q++)if((*q=='.'||*q=='!')&&(!q[1]||isspace((unsigned char)q[1]))){q[1]=0;break;}}
                    char *clean=trim(candidate,strlen(candidate));free(candidate);
                    if(explicit_save||(!strchr(clean,'?')&&strlen(clean)<=SPARK_MEMORY_TEXT)){
                        if(!capture_budget(capture,err)){free(clean);free(line);return 0;}
                        int added=add_fact(capture->notes,clean,err);free(clean);if(added<0){if(capture->forgotten){capture->skipped++;err[0]=0;}else {free(line);return 0;}}else capture->changed|=added;
                    }else {if(capture->forgotten&&strlen(clean)>SPARK_MEMORY_TEXT)capture->skipped++;free(clean);}
                }
            }
        }free(line);p=*end?end+1:end;
    }
    return 1;
}
int remember_message(cJSON *state,const char *text,char err[512]){
    err[0]=0;if(!*str(state,"selectedModel"))return 0;
    cJSON *notes=memory_notes(state);Capture capture={notes?cJSON_Duplicate(notes,1):cJSON_CreateArray(),NULL,memory_option(state,"enabled"),memory_option(state,"automatic"),0,0,NULL};
    if(!capture_message(&capture,text,err)){cJSON_Delete(capture.notes);return -1;}
    if(!capture.changed){cJSON_Delete(capture.notes);return 0;}
    return commit(state,capture.notes,capture.enabled,capture.automatic,err)?1:-1;
}
static int same_fact(const char *a,const char *b){return !_stricmp(a,b);}
int learn_chat_memories(cJSON *state,MemoryScan *scan,char err[512]){
    memset(scan,0,sizeof(*scan));err[0]=0;const char *model=str(state,"selectedModel");
    if(!*model){strcpy(err,"Select a model before learning from saved chats.");return 0;}
    cJSON *chats=cJSON_CreateArray(),*thread;int remaining=8192;
    // Replay corrections and forget commands in each conversation's own order.
    // Thread creation dates cannot establish the order of interleaved chats.
    cJSON_ArrayForEach(thread,get(state,"threads"))if(!strcmp(str(thread,"model"),model)){
        scan->conversations++;Capture capture={cJSON_CreateArray(),cJSON_CreateArray(),1,1,0,0,&remaining};cJSON *message;
        cJSON_ArrayForEach(message,get(thread,"messages"))if(!strcmp(str(message,"role"),"user")){
            scan->messages++;if(!capture_message(&capture,str(message,"content"),err)){cJSON_Delete(capture.notes);cJSON_Delete(capture.forgotten);cJSON_Delete(chats);return 0;}
        }
        scan->skipped+=capture.skipped;cJSON *chat=cJSON_CreateObject();cJSON_AddItemToObject(chat,"notes",capture.notes);cJSON_AddItemToObject(chat,"forgotten",capture.forgotten);cJSON_AddItemToArray(chats,chat);
    }
    cJSON *saved=memory_notes(state),*notes=saved?cJSON_Duplicate(saved,1):cJSON_CreateArray(),*chat,*candidate;
    cJSON_ArrayForEach(chat,chats)cJSON_ArrayForEach(candidate,get(chat,"notes")){
        const char *text=candidate->valuestring;int key=slot(text),duplicate=0,conflict=0,forgotten=0;cJSON *note,*other;
        // Existing notes (including edits in the preview) always take priority.
        cJSON_ArrayForEach(note,saved){if(same_fact(note->valuestring,text))duplicate=1;else if(key&&slot(note->valuestring)==key)conflict=1;}
        cJSON_ArrayForEach(other,chats)if(other!=chat){
            cJSON_ArrayForEach(note,get(other,"forgotten"))if(same_fact(note->valuestring,"all")||same_fact(note->valuestring,text)||(key&&slot(note->valuestring)==key))forgotten=1;
            if(key)cJSON_ArrayForEach(note,get(other,"notes"))if(slot(note->valuestring)==key&&!same_fact(note->valuestring,text))conflict=1;
        }
        cJSON_ArrayForEach(note,notes)if(same_fact(note->valuestring,text))duplicate=1;
        if(duplicate)continue;if(forgotten){scan->skipped++;continue;}if(conflict){scan->conflicts++;continue;}
        note=cJSON_CreateString(text);cJSON_AddItemToArray(notes,note);
        if(!bounded(notes,err)){cJSON_Delete(cJSON_DetachItemViaPointer(notes,note));scan->skipped++;err[0]=0;}else scan->added++;
    }
    cJSON_Delete(chats);
    if(!scan->added){cJSON_Delete(notes);return 1;}
    return commit(state,notes,memory_option(state,"enabled"),memory_option(state,"automatic"),err);
}
