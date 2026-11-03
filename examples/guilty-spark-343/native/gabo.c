#define _CRT_SECURE_NO_WARNINGS
#include "spark.h"
#include <bcrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// The journal and imported messages are saved in the same atomic workspace.
// It records consumed content, so deleting a memory does not make a poll relearn it.
int spark_digest(const char *a,const char *b,char out[65]){
    BCRYPT_ALG_HANDLE alg=NULL;BCRYPT_HASH_HANDLE hash=NULL;unsigned char bytes[32];int ok=0;
    if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,NULL,0)<0)goto done;
    if(BCryptCreateHash(alg,&hash,NULL,0,NULL,0,0)<0)goto done;
    if(BCryptHashData(hash,(PUCHAR)a,(ULONG)strlen(a),0)<0||BCryptHashData(hash,(PUCHAR)b,(ULONG)strlen(b),0)<0||BCryptFinishHash(hash,bytes,32,0)<0)goto done;
    for(int i=0;i<32;i++)sprintf(out+i*2,"%02x",bytes[i]);ok=1;
done:if(hash)BCryptDestroyHash(hash);if(alg)BCryptCloseAlgorithmProvider(alg,0);return ok;
}
static int ishash(const char *s){if(strlen(s)!=64)return 0;for(;*s;s++)if(!((*s>='0'&&*s<='9')||(*s>='a'&&*s<='f')))return 0;return 1;}
static int has(const cJSON *array,const char *text){cJSON *v;cJSON_ArrayForEach(v,array)if(cJSON_IsString(v)&&!strcmp(v->valuestring,text))return 1;return 0;}
static cJSON *hashes(const cJSON *messages){cJSON *out=cJSON_CreateArray(),*m;cJSON_ArrayForEach(m,messages){char h[65];if(!spark_digest(str(m,"role"),str(m,"content"),h)){cJSON_Delete(out);return NULL;}cJSON_AddItemToArray(out,cJSON_CreateString(h));}return out;}
static int same_prefix(const cJSON *a,const cJSON *b,int n){if(cJSON_GetArraySize(a)<n||cJSON_GetArraySize(b)<n)return 0;for(int i=0;i<n;i++){cJSON *x=cJSON_GetArrayItem(a,i),*y=cJSON_GetArrayItem(b,i);if(!cJSON_IsString(x)||!cJSON_IsString(y)||strcmp(x->valuestring,y->valuestring))return 0;}return 1;}
int import_gabo_journal(cJSON *state,const cJSON *source,char error[512]){
    const cJSON *journal=get(source,"gaboImports");if(!journal)return 1;
    if(!cJSON_IsObject(journal)||cJSON_GetArraySize(journal)>1000){strcpy(error,"Invalid Gabo net import journal.");return 0;}
    cJSON *v;cJSON_ArrayForEach(v,journal){
        if(!v->string||!ishash(v->string)||!cJSON_IsObject(v)||!*str(v,"model")||strlen(str(v,"model"))>SPARK_TEXT||!*str(v,"destinationId")||strlen(str(v,"destinationId"))>=128){strcpy(error,"Invalid Gabo net conversation provenance.");return 0;}
        const char *fields[]={"hashes","seen"};for(int i=0;i<2;i++){cJSON *array=get(v,fields[i]),*h;if(!cJSON_IsArray(array)||cJSON_GetArraySize(array)>(i?5000:1000)){strcpy(error,"Gabo net import history exceeds its limit.");return 0;}cJSON_ArrayForEach(h,array)if(!cJSON_IsString(h)||!ishash(h->valuestring)){strcpy(error,"Invalid Gabo net content fingerprint.");return 0;}}
        cJSON *copy=cJSON_CreateObject();setstr(copy,"model",str(v,"model"));setstr(copy,"destinationId",str(v,"destinationId"));cJSON_AddItemToObject(copy,"hashes",cJSON_Duplicate(get(v,"hashes"),1));cJSON_AddItemToObject(copy,"seen",cJSON_Duplicate(get(v,"seen"),1));cJSON_AddItemToObject(get(state,"gaboImports"),v->string,copy);
    }return 1;
}
static const char *canonical(const char *label,const cJSON *aliases){
    if(!*label)return "";const char *known=str(aliases,label);if(*known)return known;
    // Only exact, explicitly registered labels are accepted. No default model.
    return "";
}
static const char *attributed(const cJSON *thread,const cJSON *message,const cJSON *telemetry,const cJSON *aliases){
    const char *label=*str(message,"model")?str(message,"model"):str(thread,"model");
    const char *model=canonical(label,aliases);if(*label&&!*model)return "";
    cJSON *stamp=get(message,"timestamp"),*row;double ts=cJSON_IsNumber(stamp)?stamp->valuedouble:0;const char *evidence="";
    if(ts>0)cJSON_ArrayForEach(row,telemetry)if(!strcmp(str(row,"thread_id"),str(thread,"id"))){cJSON *time=get(row,"ts");
        if(cJSON_IsNumber(time)&&fabs(time->valuedouble-ts)<=10000){const char *found=canonical(str(row,"model_id"),aliases);if(!*found)return "";if(*evidence&&strcmp(evidence,found))return "";evidence=found;}}
    if(*model&&*evidence&&strcmp(model,evidence))return "";return *model?model:evidence;
}
static cJSON *group_for(cJSON *groups,const char *model){cJSON *g;cJSON_ArrayForEach(g,groups)if(!strcmp(str(g,"model"),model))return g;g=cJSON_CreateObject();setstr(g,"model",model);cJSON_AddArrayToObject(g,"messages");cJSON_AddItemToArray(groups,g);return g;}
static int add_message(cJSON *array,const cJSON *m,char error[512]){
    const char *role=str(m,"role"),*content=str(m,"content");
    if((strcmp(role,"user")&&strcmp(role,"assistant"))||!cJSON_IsString(get(m,"content"))||strlen(content)>SPARK_TEXT||get(m,"tool_calls")||get(m,"function_call")){strcpy(error,"Unsupported message in Gabo net source.");return 0;}
    cJSON *copy=cJSON_CreateObject();char id[64];newid(id);setstr(copy,"id",id);setstr(copy,"role",role);setstr(copy,"content",content);cJSON_AddItemToArray(array,copy);return 1;
}
static cJSON *split_thread(const cJSON *t,const cJSON *telemetry,const cJSON *aliases,GaboScan *scan,char error[512]){
    cJSON *groups=cJSON_CreateArray(),*pending=cJSON_CreateArray(),*m,*messages=get(t,"messages");
    if(!*str(t,"id")||!cJSON_IsArray(messages)||cJSON_GetArraySize(messages)>1000){strcpy(error,"Gabo net conversations need stable IDs and at most 1,000 messages.");goto fail;}
    cJSON_ArrayForEach(m,messages){const char *role=str(m,"role");
        if(!strcmp(role,"user")){if(!add_message(pending,m,error))goto fail;}
        else if(!strcmp(role,"assistant")){
            const char *model=attributed(t,m,telemetry,aliases);
            if(!*model||!*str(m,"content")){scan->pending++;}
            else {cJSON *g=group_for(groups,model),*out=get(g,"messages"),*u;cJSON_ArrayForEach(u,pending)if(!add_message(out,u,error))goto fail;if(!add_message(out,m,error))goto fail;}
            cJSON_Delete(pending);pending=cJSON_CreateArray();
        }else{strcpy(error,"Gabo net source contains an unsupported message role.");goto fail;}
    }
    if(pending->child)scan->pending++;cJSON_Delete(pending);return groups;
fail:cJSON_Delete(pending);cJSON_Delete(groups);return NULL;
}
static cJSON *imported_thread(const cJSON *src,const cJSON *group,const char *id,int branch){
    cJSON *t=new_thread(*str(src,"name")?str(src,"name"):"Imported Gabo net conversation");setstr(t,"id",id);setstr(t,"model",str(group,"model"));setstr(t,"context",str(src,"lore"));
    if(branch){Buffer name={0};adds(&name,str(t,"name"));adds(&name," [Gabo net update]");if(name.n<=200)setstr(t,"name",name.p);free(name.p);}
    cJSON_ReplaceItemInObjectCaseSensitive(t,"pinned",cJSON_CreateBool(cJSON_IsTrue(get(src,"pinned"))));
    if(get(src,"createdAt")){cJSON_DeleteItemFromObjectCaseSensitive(t,"createdAt");cJSON_AddItemToObject(t,"createdAt",cJSON_Duplicate(get(src,"createdAt"),1));}
    cJSON_ReplaceItemInObjectCaseSensitive(t,"messages",cJSON_Duplicate(get(group,"messages"),1));return t;
}
cJSON *gabo_merge(const cJSON *state,const cJSON *source,const char *origin,const cJSON *aliases,GaboScan *scan,char error[512]){
    memset(scan,0,sizeof(*scan));error[0]=0;
    cJSON *threads=get(source,"threads"),*telemetry=get(source,"telemetry");
    if(!*origin||strlen(origin)>2048||(!cJSON_IsArray(threads)&&!cJSON_IsObject(threads))||cJSON_GetArraySize(threads)>100||!cJSON_IsObject(aliases)||(telemetry&&!cJSON_IsArray(telemetry))){strcpy(error,"Invalid Gabo net source or model mappings.");return NULL;}
    cJSON *out=cJSON_Duplicate(state,1),*learning=cJSON_CreateObject(),*activity=cJSON_CreateArray(),*t;
    if(!get(out,"gaboImports"))cJSON_AddObjectToObject(out,"gaboImports");
    cJSON_ArrayForEach(t,threads){int pendingBefore=scan->pending;cJSON *groups=split_thread(t,telemetry,aliases,scan,error);if(!groups)goto fail;cJSON *group;
        // A missing old generation label must never look like a source edit or
        // truncate an imported branch. Retry the whole chat when attribution
        // becomes complete, including when the source is still streaming.
        if(scan->pending!=pendingBefore){cJSON_Delete(groups);continue;}
        cJSON_ArrayForEach(group,groups){
            Buffer identity={0};adds(&identity,origin);adds(&identity,str(t,"id"));char legacyHash[65],key[65];
            if(!spark_digest(identity.p,"",legacyHash)){free(identity.p);cJSON_Delete(groups);strcpy(error,"Cannot fingerprint import.");goto fail;}
            adds(&identity,"\n");adds(&identity,str(group,"model"));spark_digest(identity.p,"",key);free(identity.p);
            cJSON *journal=get(out,"gaboImports"),*record=get(journal,key),*fresh=hashes(get(group,"messages"));
            if(!fresh){cJSON_Delete(groups);strcpy(error,"Cannot fingerprint Gabo net messages.");goto fail;}
            int count=cJSON_GetArraySize(fresh),oldCount=record?cJSON_GetArraySize(get(record,"hashes")):0;
            if(record&&same_prefix(fresh,get(record,"hashes"),count)&&count==oldCount){cJSON_Delete(fresh);continue;}
            cJSON *target=record?find_id(get(out,"threads"),str(record,"destinationId")):NULL;
            if(record&&!target){scan->skipped++;cJSON_Delete(fresh);continue;} // Respect a local deletion.
            if(target&&strcmp(str(target,"model"),str(group,"model"))){cJSON_Delete(fresh);cJSON_Delete(groups);strcpy(error,"Import journal points to another model's conversation.");goto fail;}
            char id[128];snprintf(id,sizeof(id),"gobbonet-%.32s",legacyHash);
            if(!record){
                if(cJSON_GetArraySize(journal)>=1000){cJSON_Delete(fresh);cJSON_Delete(groups);strcpy(error,"Gabo net import journal is full.");goto fail;}
                // Adopt the earlier verified manual import only when its exact
                // content is a prefix and its model agrees; never seize a chat.
                cJSON *prior=find_id(get(out,"threads"),id),*priorHashes=prior?hashes(get(prior,"messages")):NULL;
                int n=cJSON_GetArraySize(priorHashes);
                if(prior&&!strcmp(str(prior,"model"),str(group,"model"))&&n&&same_prefix(fresh,priorHashes,n)){target=prior;oldCount=n;}
                else snprintf(id,sizeof(id),"gabo-%.32s",key);
                record=cJSON_CreateObject();setstr(record,"model",str(group,"model"));setstr(record,"destinationId",target?str(target,"id"):id);cJSON_AddItemToObject(record,"hashes",target?cJSON_Duplicate(priorHashes,1):cJSON_CreateArray());cJSON_AddArrayToObject(record,"seen");
                if(target){cJSON *m;int i=0;cJSON_ArrayForEach(m,get(prior,"messages")){if(!strcmp(str(m,"role"),"user"))cJSON_AddItemToArray(get(record,"seen"),cJSON_Duplicate(cJSON_GetArrayItem(priorHashes,i),1));i++;}}
                cJSON_AddItemToObject(journal,key,record);cJSON_Delete(priorHashes);scan->changed=1;
            }
            int appendOnly=target&&same_prefix(fresh,get(record,"hashes"),oldCount)&&count>=oldCount;
            cJSON *localHashes=target?hashes(get(target,"messages")):NULL;
            if(target&&(!appendOnly||cJSON_GetArraySize(localHashes)!=oldCount||!same_prefix(localHashes,get(record,"hashes"),oldCount))){
                if(appendOnly&&count==oldCount){cJSON_Delete(localHashes);cJSON_Delete(fresh);continue;}
                scan->conflicts++;newid(id);target=NULL;setstr(record,"destinationId",id);
            }cJSON_Delete(localHashes);
            if(!target){
                if(cJSON_GetArraySize(get(out,"threads"))>=100){cJSON_Delete(fresh);cJSON_Delete(groups);strcpy(error,"The workspace is full; Gabo net import will retry after space is available.");goto fail;}
                if(find_id(get(out,"threads"),str(record,"destinationId"))){cJSON_Delete(fresh);cJSON_Delete(groups);strcpy(error,"Gabo net import ID collides with an existing chat.");goto fail;}
                target=imported_thread(t,group,str(record,"destinationId"),oldCount>0);cJSON_AddItemToArray(get(out,"threads"),target);scan->added++;scan->messages+=count;
                cJSON *row=activity_model(activity,str(group,"model"));setnum(row,"added",get(row,"added")->valueint+1);setnum(row,"messages",get(row,"messages")->valueint+count);
            }else if(count>oldCount){for(int i=oldCount;i<count;i++)cJSON_AddItemToArray(get(target,"messages"),cJSON_Duplicate(cJSON_GetArrayItem(get(group,"messages"),i),1));scan->updated++;scan->messages+=count-oldCount;
                cJSON *row=activity_model(activity,str(group,"model"));setnum(row,"updated",get(row,"updated")->valueint+1);setnum(row,"messages",get(row,"messages")->valueint+count-oldCount);}
            cJSON *deltas=get(learning,str(group,"model"));if(!deltas){deltas=cJSON_CreateArray();cJSON_AddItemToObject(learning,str(group,"model"),deltas);}
            cJSON *delta=new_thread("New imported messages");setstr(delta,"model",str(group,"model"));cJSON *m;int i=0;
            cJSON_ArrayForEach(m,get(group,"messages")){const char *h=cJSON_GetArrayItem(fresh,i++)->valuestring;
                if(!strcmp(str(m,"role"),"user")&&!has(get(record,"seen"),h)){
                    if(cJSON_GetArraySize(get(record,"seen"))>=5000){cJSON_Delete(delta);cJSON_Delete(fresh);cJSON_Delete(groups);strcpy(error,"Gabo net learned-message history is full.");goto fail;}
                    cJSON_AddItemToArray(get(record,"seen"),cJSON_CreateString(h));cJSON_AddItemToArray(get(delta,"messages"),cJSON_Duplicate(m,1));}}
            cJSON_AddItemToArray(deltas,delta);cJSON_ReplaceItemInObjectCaseSensitive(record,"hashes",fresh);scan->changed=1;
            if(!get(get(out,"modelThreads"),str(group,"model")))setstr(get(out,"modelThreads"),str(group,"model"),str(target,"id"));
        }cJSON_Delete(groups);
    }
    cJSON *deltas;cJSON_ArrayForEach(deltas,learning){
        cJSON *view=cJSON_Duplicate(out,1);setstr(view,"selectedModel",deltas->string);cJSON_ReplaceItemInObjectCaseSensitive(view,"threads",cJSON_Duplicate(deltas,1));
        if(memory_option(view,"enabled")&&memory_option(view,"automatic")){
            MemoryScan learned={0};if(!learn_chat_memories(view,&learned,error)){cJSON_Delete(view);goto fail;}scan->memories+=learned.added;scan->skipped+=learned.skipped;scan->conflicts+=learned.conflicts;
            cJSON_ReplaceItemInObjectCaseSensitive(out,"modelMemories",cJSON_Duplicate(get(view,"modelMemories"),1));
        }cJSON_Delete(view);
    }
    // Receipts join chats, memories and provenance in the same atomic commit.
    if(!record_gabo_activity(out,state,activity,error))goto fail;
    // Reuse the product's full schema/size validation before the caller commits.
    char *json=cJSON_PrintUnformatted(out);cJSON *valid=import_state(json,NULL,error);free(json);cJSON_Delete(out);cJSON_Delete(learning);cJSON_Delete(activity);return valid;
fail:cJSON_Delete(out);cJSON_Delete(learning);cJSON_Delete(activity);return NULL;
}
