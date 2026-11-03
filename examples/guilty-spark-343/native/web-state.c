#define _CRT_SECURE_NO_WARNINGS
#include "spark.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void replace(cJSON *o,const char *key,cJSON *value){cJSON_DeleteItemFromObjectCaseSensitive(o,key);cJSON_AddItemToObject(o,key,value);}
static int identity(const char *s){if(!*s||strlen(s)>=128)return 0;for(;*s;s++)if(!((*s>='a'&&*s<='z')||(*s>='A'&&*s<='Z')||(*s>='0'&&*s<='9')||*s=='_'||*s=='-'))return 0;return 1;}
static int unique_ids(const cJSON *array){cJSON *a,*b;cJSON_ArrayForEach(a,array){if(!identity(str(a,"id")))return 0;for(b=a->next;b;b=b->next)if(!strcmp(str(a,"id"),str(b,"id")))return 0;}return 1;}
static int is_note(const cJSON *m){return !strcmp(str(m,"role"),"system")&&cJSON_IsTrue(get(m,"archived"))&&cJSON_IsTrue(get(m,"cardNote"));}
static cJSON *card_for_web(const cJSON *native,const cJSON *prior){
    cJSON *card=prior?cJSON_Duplicate(prior,1):cJSON_CreateObject();const char *direct[]={"id","name","personality"};for(int i=0;i<3;i++)setstr(card,direct[i],str(native,direct[i]));
    setstr(card,"writingStyle",str(native,"systemPrompt"));setstr(card,"description",str(native,"description"));setstr(card,"startingLore",str(native,"scenario"));setstr(card,"greeting",str(native,"greeting"));return card;
}
cJSON *web_profile(const cJSON *state,const char *model){
    cJSON *saved=get(get(state,"webProfiles"),model),*out=saved?cJSON_Duplicate(saved,1):cJSON_CreateObject(),*threads=cJSON_CreateArray(),*t;
    cJSON_ArrayForEach(t,get(state,"threads"))if(!strcmp(str(t,"model"),model)){
        cJSON *prior=find_id(get(saved,"threads"),str(t,"id")),*copy=prior?cJSON_Duplicate(prior,1):cJSON_CreateObject(),*messages=cJSON_CreateArray(),*m;
        const char *keys[]={"id","name","createdAt","draft"};for(int i=0;i<4;i++)setstr(copy,keys[i],str(t,keys[i]));setstr(copy,"model",model);setstr(copy,"lore",str(t,"context"));replace(copy,"pinned",cJSON_CreateBool(cJSON_IsTrue(get(t,"pinned"))));
        for(m=get(prior,"messages")?get(prior,"messages")->child:NULL;m&&is_note(m);m=m->next)cJSON_AddItemToArray(messages,cJSON_Duplicate(m,1));
        cJSON_ArrayForEach(m,get(t,"messages")){cJSON *old=find_id(get(prior,"messages"),str(m,"id")),*entry=old?cJSON_Duplicate(old,1):cJSON_CreateObject();setstr(entry,"id",str(m,"id"));setstr(entry,"role",str(m,"role"));setstr(entry,"content",str(m,"content"));cJSON_AddItemToArray(messages,entry);for(cJSON *note=old?old->next:NULL;note&&is_note(note);note=note->next)cJSON_AddItemToArray(messages,cJSON_Duplicate(note,1));}replace(copy,"messages",messages);cJSON_AddItemToArray(threads,copy);
    }replace(out,"threads",threads);cJSON_DeleteItemFromObjectCaseSensitive(out,"threadOrder");setstr(out,"activeThreadId",str(get(state,"modelThreads"),model));
    cJSON *cards=cJSON_CreateArray(),*c;cJSON_ArrayForEach(c,get(state,"characters"))cJSON_AddItemToArray(cards,card_for_web(c,find_id(get(saved,"characterCards"),str(c,"id"))));replace(out,"characterCards",cards);setstr(out,"activeCardId",str(get(state,"modelCharacters"),model));
    if(!get(out,"settings"))cJSON_AddObjectToObject(out,"settings");cJSON_DeleteItemFromObjectCaseSensitive(get(out,"settings"),"apiKey");
    if(!get(out,"personaCards")&&*str(state,"persona")){cJSON *personas=cJSON_AddArrayToObject(out,"personaCards"),*persona=cJSON_CreateObject();setstr(persona,"id","native-persona");setstr(persona,"name","User");setstr(persona,"description",str(state,"persona"));setnum(persona,"injectionFrequency",1);cJSON_AddItemToArray(personas,persona);setstr(out,"activePersonaId","native-persona");}
    setstr(out,"sparkModel",model);cJSON *profile=get(get(state,"modelMemories"),model);replace(out,"sparkMemories",profile?cJSON_Duplicate(profile,1):cJSON_Parse("{\"enabled\":true,\"automatic\":true,\"notes\":[]}"));return out;
}
cJSON *web_save_profile(const cJSON *state,const cJSON *profile,const char *model,char error[512]){
    error[0]=0;if(!*model||strlen(model)>SPARK_TEXT||!cJSON_IsObject(profile)||!cJSON_IsArray(get(profile,"threads"))){strcpy(error,"Invalid model workspace.");return NULL;}
    if(!unique_ids(get(profile,"threads"))||!unique_ids(get(profile,"characterCards"))){strcpy(error,"Conversations and characters require unique stable IDs.");return NULL;}
    cJSON *t,*m,*card;cJSON_ArrayForEach(t,get(profile,"threads")){
        if(!identity(str(t,"id"))||!cJSON_IsArray(get(t,"messages"))){strcpy(error,"Conversations need stable IDs and message arrays.");return NULL;}
        if(*str(t,"model")&&strcmp(str(t,"model"),model)){strcpy(error,"A conversation is labeled for a different model. Open its model workspace to import it.");return NULL;}
        cJSON *existing=find_id(get(state,"threads"),str(t,"id"));if(existing&&strcmp(str(existing,"model"),model)){strcpy(error,"This conversation belongs to another model.");return NULL;}
        if(!unique_ids(get(t,"messages"))){strcpy(error,"Messages require unique stable IDs within each conversation.");return NULL;}
    }
    cJSON_ArrayForEach(card,get(profile,"characterCards"))if(!identity(str(card,"id"))){strcpy(error,"Characters need stable IDs.");return NULL;}
    // Normalize the text projection through the existing bounded importer.
    cJSON *raw=cJSON_Duplicate(profile,1);setstr(raw,"selectedModel",model);cJSON_DeleteItemFromObjectCaseSensitive(raw,"version");cJSON_DeleteItemFromObjectCaseSensitive(raw,"gaboImports");cJSON_DeleteItemFromObjectCaseSensitive(raw,"webProfiles");cJSON_DeleteItemFromObjectCaseSensitive(raw,"modelMemories");
    cJSON *projection=cJSON_Duplicate(raw,1);cJSON_ArrayForEach(t,get(projection,"threads")){m=get(t,"messages")->child;while(m){cJSON *next=m->next;if(is_note(m))cJSON_Delete(cJSON_DetachItemViaPointer(get(t,"messages"),m));m=next;}}
    char *json=cJSON_PrintUnformatted(projection);cJSON_Delete(projection);cJSON *normalized=import_state(json,NULL,error);free(json);if(!normalized){cJSON_Delete(raw);return NULL;}
    cJSON *out=cJSON_Duplicate(state,1),*all=get(out,"threads");t=all->child;while(t){cJSON *next=t->next;if(!strcmp(str(t,"model"),model))cJSON_Delete(cJSON_DetachItemViaPointer(all,t));t=next;}
    cJSON_ArrayForEach(t,get(normalized,"threads"))cJSON_AddItemToArray(all,cJSON_Duplicate(t,1));
    if(cJSON_IsArray(get(profile,"characterCards"))){card=get(out,"characters")->child;while(card){cJSON *next=card->next;if(!find_id(get(normalized,"characters"),str(card,"id")))cJSON_Delete(cJSON_DetachItemViaPointer(get(out,"characters"),card));card=next;}}
    cJSON_ArrayForEach(card,get(normalized,"characters")){cJSON *cards=get(out,"characters"),*old=find_id(cards,str(card,"id"));if(old)cJSON_ReplaceItemViaPointer(cards,old,cJSON_Duplicate(card,1));else cJSON_AddItemToArray(cards,cJSON_Duplicate(card,1));}
    const char *active=str(normalized,"activeThreadId");setstr(get(out,"modelThreads"),model,active);const char *character=str(normalized,"activeCharacterId");setstr(get(out,"modelCharacters"),model,character);
    if(!strcmp(str(state,"selectedModel"),model)){setstr(out,"activeThreadId",active);setstr(out,"activeCharacterId",character);}
    char *selection=_strdup(str(out,"selectedModel"));setstr(out,"selectedModel",model);
    cJSON_ArrayForEach(t,get(normalized,"threads")){cJSON *prior=find_id(get(state,"threads"),str(t,"id"));cJSON_ArrayForEach(m,get(t,"messages"))if(!strcmp(str(m,"role"),"user")){cJSON *old=find_id(get(prior,"messages"),str(m,"id"));if(!old){cJSON *other;cJSON_ArrayForEach(other,get(state,"threads"))if(!strcmp(str(other,"model"),model)){cJSON *same=find_id(get(other,"messages"),str(m,"id"));if(same&&!strcmp(str(same,"content"),str(m,"content"))){old=same;break;}}}if(!old||strcmp(str(old,"content"),str(m,"content"))){char memoryError[512]={0};if(remember_message(out,str(m,"content"),memoryError)<0)setstr(raw,"sparkMemoryWarning",memoryError);}}}
    setstr(out,"selectedModel",selection);free(selection);
    if(!get(out,"webProfiles"))cJSON_AddObjectToObject(out,"webProfiles");
    cJSON_DeleteItemFromObjectCaseSensitive(get(raw,"settings"),"apiKey");cJSON_DeleteItemFromObjectCaseSensitive(raw,"sparkMemories");cJSON_DeleteItemFromObjectCaseSensitive(raw,"sparkModel");cJSON_DeleteItemFromObjectCaseSensitive(raw,"sparkRevision");
    replace(get(out,"webProfiles"),model,raw);cJSON_Delete(normalized);json=cJSON_PrintUnformatted(out);cJSON *valid=import_state(json,NULL,error);free(json);cJSON_Delete(out);return valid;
}
