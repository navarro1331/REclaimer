#define _CRT_SECURE_NO_WARNINGS
#include "spark.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define ACTIVITY_EVENTS 50
#define ACTIVITY_BYTES (256u*1024u)
void spark_utc(char out[32]){
    SYSTEMTIME t;GetSystemTime(&t);snprintf(out,32,"%04u-%02u-%02uT%02u:%02u:%02uZ",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond);
}
static int timestamp(const char *text,SYSTEMTIME *t){
    unsigned y,m,d,h,n,s;char extra;memset(t,0,sizeof(*t));
    if(strlen(text)!=20||sscanf(text,"%4u-%2u-%2uT%2u:%2u:%2uZ%c",&y,&m,&d,&h,&n,&s,&extra)!=6||y<1601||y>9999||m<1||m>12||d<1||d>31||h>23||n>59||s>59)return 0;
    char canonical[32];snprintf(canonical,sizeof(canonical),"%04u-%02u-%02uT%02u:%02u:%02uZ",y,m,d,h,n,s);if(strcmp(text,canonical))return 0;
    t->wYear=(WORD)y;t->wMonth=(WORD)m;t->wDay=(WORD)d;t->wHour=(WORD)h;t->wMinute=(WORD)n;t->wSecond=(WORD)s;
    FILETIME ft;SYSTEMTIME normalized;return SystemTimeToFileTime(t,&ft)&&FileTimeToSystemTime(&ft,&normalized)&&normalized.wYear==y&&normalized.wMonth==m&&normalized.wDay==d;
}
int activity_time_valid(const char *text){SYSTEMTIME t;return timestamp(text,&t);}
static void display_time(Buffer *out,const char *text,const char *missing){
    SYSTEMTIME utc,local;if(!timestamp(text,&utc)){adds(out,missing);return;}
    if(!SystemTimeToTzSpecificLocalTime(NULL,&utc,&local))local=utc;
    char line[96];snprintf(line,sizeof(line),"%04u-%02u-%02u %02u:%02u:%02u (local time)",local.wYear,local.wMonth,local.wDay,local.wHour,local.wMinute,local.wSecond);adds(out,line);
}
static int count_valid(const cJSON *o,const char *key){cJSON *n=get(o,key);return cJSON_IsNumber(n)&&n->valuedouble>=0&&n->valuedouble<=5000&&floor(n->valuedouble)==n->valuedouble;}
static int digest_valid(const cJSON *v){if(!cJSON_IsString(v)||strlen(v->valuestring)!=64)return 0;for(const char *p=v->valuestring;*p;p++)if(!((*p>='0'&&*p<='9')||(*p>='a'&&*p<='f')))return 0;return 1;}
int import_gabo_activity(cJSON *state,const cJSON *source,char error[512]){
    cJSON *events=get(source,"gaboActivity");if(!events)return 1;
    if(!cJSON_IsArray(events)||cJSON_GetArraySize(events)>ACTIVITY_EVENTS)goto invalid;
    char *json=cJSON_PrintUnformatted(events);size_t size=strlen(json);free(json);if(size>ACTIVITY_BYTES)goto invalid;
    cJSON *e,*r,*n;cJSON_ArrayForEach(e,events){
        if(!cJSON_IsObject(e)||!activity_time_valid(str(e,"at"))||!cJSON_IsArray(get(e,"models"))||!get(e,"models")->child||cJSON_GetArraySize(get(e,"models"))>1000)goto invalid;
        cJSON_ArrayForEach(r,get(e,"models")){
            if(!cJSON_IsObject(r)||!*str(r,"model")||strlen(str(r,"model"))>SPARK_TEXT||!count_valid(r,"added")||!count_valid(r,"updated")||!count_valid(r,"messages")||!cJSON_IsArray(get(r,"learned"))||cJSON_GetArraySize(get(r,"learned"))>SPARK_MEMORY_ITEMS)goto invalid;
            for(cJSON *other=r->next;other;other=other->next)if(!strcmp(str(r,"model"),str(other,"model")))goto invalid;
            cJSON_ArrayForEach(n,get(r,"learned"))if(!digest_valid(n))goto invalid;
        }
    }
    // Rebuild only recognized fields. Activity cannot smuggle copies of deleted
    // facts (or arbitrary backup fields) into a saved receipt.
    cJSON *copy=cJSON_CreateArray();cJSON_ArrayForEach(e,events){cJSON *entry=cJSON_CreateObject();setstr(entry,"at",str(e,"at"));cJSON *rows=cJSON_AddArrayToObject(entry,"models");cJSON_ArrayForEach(r,get(e,"models")){cJSON *row=cJSON_CreateObject();setstr(row,"model",str(r,"model"));setnum(row,"added",get(r,"added")->valueint);setnum(row,"updated",get(r,"updated")->valueint);setnum(row,"messages",get(r,"messages")->valueint);cJSON_AddItemToObject(row,"learned",cJSON_Duplicate(get(r,"learned"),1));cJSON_AddItemToArray(rows,row);}cJSON_AddItemToArray(copy,entry);}
    cJSON_DeleteItemFromObjectCaseSensitive(state,"gaboActivity");cJSON_AddItemToObject(state,"gaboActivity",copy);return 1;
invalid:strcpy(error,"Invalid Gabo net activity history (up to 50 import batches / 256 KiB).");return 0;
}
cJSON *activity_model(cJSON *rows,const char *model){
    cJSON *row;cJSON_ArrayForEach(row,rows)if(!strcmp(str(row,"model"),model))return row;
    row=cJSON_CreateObject();setstr(row,"model",model);setnum(row,"added",0);setnum(row,"updated",0);setnum(row,"messages",0);cJSON_AddArrayToObject(row,"learned");cJSON_AddItemToArray(rows,row);return row;
}
int record_gabo_activity(cJSON *state,const cJSON *before,cJSON *rows,char error[512]){
    if(!rows->child)return 1;
    cJSON *row,*note;cJSON_ArrayForEach(row,rows){
        const char *model=str(row,"model");cJSON *old=get(get(get(before,"modelMemories"),model),"notes");
        cJSON_ArrayForEach(note,get(get(get(state,"modelMemories"),model),"notes")){
            int found=0;cJSON *n;cJSON_ArrayForEach(n,old)if(!_stricmp(n->valuestring,note->valuestring))found=1;
            if(!found){char hash[65];if(!spark_digest(model,note->valuestring,hash)){strcpy(error,"Cannot fingerprint learned memory for import activity.");return 0;}cJSON_AddItemToArray(get(row,"learned"),cJSON_CreateString(hash));}
        }
    }
    cJSON *events=get(state,"gaboActivity");if(!events)events=cJSON_AddArrayToObject(state,"gaboActivity");
    cJSON *entry=cJSON_CreateObject();char now[32];spark_utc(now);setstr(entry,"at",now);cJSON_AddItemToObject(entry,"models",cJSON_Duplicate(rows,1));cJSON_InsertItemInArray(events,0,entry);
    for(;;){char *json=cJSON_PrintUnformatted(events);size_t size=strlen(json);free(json);if(cJSON_GetArraySize(events)<=ACTIVITY_EVENTS&&size<=ACTIVITY_BYTES)break;if(cJSON_GetArraySize(events)==1){strcpy(error,"This import's activity exceeds 256 KiB. No imported data was saved.");return 0;}cJSON_DeleteItemFromArray(events,cJSON_GetArraySize(events)-1);}
    return 1;
}
static const char *resolve_note(const cJSON *state,const char *model,const char *hash){
    cJSON *n;cJSON_ArrayForEach(n,get(get(get(state,"modelMemories"),model),"notes")){char current[65];if(spark_digest(model,n->valuestring,current)&&!strcmp(hash,current))return n->valuestring;}return NULL;
}
char *gabo_activity_text(const cJSON *state,const cJSON *check,const char *model,int enabled,int checking){
    Buffer out={0};char line[240];adds(&out,enabled?"Automatic import: on\r\n":"Automatic import: off\r\n");
    adds(&out,"Last checked: ");display_time(&out,str(check,"lastCheckUtc"),"No completed check recorded");adds(&out,"\r\nLast successful check: ");display_time(&out,str(check,"lastSuccessfulCheckUtc"),"Not recorded");
    cJSON *events=get(state,"gaboActivity");adds(&out,"\r\nLast imported new content: ");display_time(&out,str(events?events->child:NULL,"at"),get(state,"gaboImports")&&get(state,"gaboImports")->child?"Not recorded for older imports":"No new content imported yet");
    if(*str(check,"lastCheckUtc")){adds(&out,cJSON_IsTrue(get(check,"succeeded"))?"\r\nLatest check succeeded. ":"\r\nLatest check failed. ");adds(&out,str(check,"summary"));}
    if(checking)adds(&out,"\r\nChecking Gabo net now; this view updates when the check finishes.");
    adds(&out,"\r\n\r\n");adds(&out,*model?model:"All models");
    if(*model){const char *last="";cJSON *e,*r;cJSON_ArrayForEach(e,events){cJSON_ArrayForEach(r,get(e,"models"))if(!strcmp(str(r,"model"),model)){last=str(e,"at");break;}if(*last)break;}adds(&out,"\r\nLast import for this model: ");display_time(&out,last,"No recorded import");}
    adds(&out,"\r\nRecent imports (newest first; up to 50 batches):\r\n");
    int shown=0;cJSON *event,*row;cJSON_ArrayForEach(event,events){int heading=0;cJSON_ArrayForEach(row,get(event,"models")){
        const char *owner=str(row,"model");if(*model&&strcmp(model,owner))continue;
        if(!heading){adds(&out,"\r\n");display_time(&out,str(event,"at"),"Unknown import time");adds(&out,"\r\n");heading=1;}
        shown++;adds(&out,owner);snprintf(line,sizeof(line)," - %d new chats, %d updated chats, %d imported messages\r\n",get(row,"added")->valueint,get(row,"updated")->valueint,get(row,"messages")->valueint);adds(&out,line);
        int learned=cJSON_GetArraySize(get(row,"learned"));snprintf(line,sizeof(line),"New memories learned: %d\r\n",learned);adds(&out,line);
        cJSON *hash;int removed=0;cJSON_ArrayForEach(hash,get(row,"learned")){const char *note=resolve_note(state,owner,hash->valuestring);if(note){adds(&out,"  - ");adds(&out,note);adds(&out,"\r\n");}else removed++;}
        if(removed){snprintf(line,sizeof(line),"  (%d learned memories were later removed or edited; their text is not retained in this log.)\r\n",removed);adds(&out,line);}
        if(!learned)adds(&out,"  No new memories were saved for this model in this import.\r\n");adds(&out,"\r\n");
    }}
    if(!shown)adds(&out,"No recorded imports for this selection. Older imports have no activity receipt.\r\n");
    adds(&out,"\r\nChecks with no new content leave this history unchanged. Learning uses each model's own memory settings.\r\n");return out.p;
}
