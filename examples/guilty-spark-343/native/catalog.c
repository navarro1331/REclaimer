#define _CRT_SECURE_NO_WARNINGS
#include "spark.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <bcrypt.h>

#define METADATA_LIMIT (32u*1024u*1024u)
#define CATALOG_LIMIT 512
typedef struct { const unsigned char *p,*end; unsigned items; HANDLE cancel; } Reader;
typedef struct { cJSON *models; HANDLE cancel; unsigned entries; int failed; char *error; size_t metadataBytes; } Scan;
static int stopped(HANDLE event){return event&&WaitForSingleObject(event,0)==WAIT_OBJECT_0;}
static int take(Reader *r,size_t n,const unsigned char **out){if(n>(size_t)(r->end-r->p)||(!(r->items&4095)&&stopped(r->cancel)))return 0;if(out)*out=r->p;r->p+=n;return 1;}
static int number(Reader *r,unsigned n,uint64_t *value){const unsigned char *p;if(!take(r,n,&p))return 0;*value=0;for(unsigned i=0;i<n;i++)*value|=(uint64_t)p[i]<<(i*8);return 1;}
static int text_value(Reader *r,char **text){uint64_t length;const unsigned char *p;if(!number(r,8,&length)||length>METADATA_LIMIT||!take(r,(size_t)length,&p))return 0;if(text){if(length>1024*1024||memchr(p,0,(size_t)length))return 0;*text=allocate((size_t)length+1);memcpy(*text,p,(size_t)length);if(length&&!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,*text,(int)length,NULL,0)){free(*text);*text=NULL;return 0;}}return 1;}
static int skip_value(Reader *r,unsigned type,unsigned depth){
    static const unsigned widths[]={1,1,2,2,4,4,4,1,0,0,8,8,8};
    if(type>12||depth>4||++r->items>2000000)return 0;if(widths[type])return take(r,widths[type],NULL);if(type==8)return text_value(r,NULL);
    uint64_t element,count;if(!number(r,4,&element)||element>12||!number(r,8,&count)||count>1000000)return 0;
    if(widths[element])return count<=METADATA_LIMIT/widths[element]&&take(r,(size_t)count*widths[element],NULL);
    for(uint64_t i=0;i<count;i++)if(!skip_value(r,(unsigned)element,depth+1))return 0;return 1;
}
static void unavailable(cJSON *model,const char *reason){cJSON_DeleteItemFromObjectCaseSensitive(model,"supported");cJSON_AddBoolToObject(model,"supported",0);setstr(model,"reason",reason);}
static void available(cJSON *model,const char *reason){cJSON_DeleteItemFromObjectCaseSensitive(model,"supported");cJSON_AddBoolToObject(model,"supported",1);setstr(model,"reason",reason);}
static int split_files(cJSON *model,char error[512]){
    int count=get(model,"splitCount")->valueint;if(count<2)return 1;
    if(count>256||!get(model,"splitNo")||get(model,"splitNo")->valueint!=0){strcpy(error,"Select the first GGUF shard (00001); all parts must be together.");return 0;}
    wchar_t *path=wide(str(model,"path"));size_t length=path?wcslen(path):0;
    if(length<20||wcsncmp(path+length-20,L"-00001-of-",10)){free(path);strcpy(error,"Split GGUF files need standard -00001-of-000NN.gguf names.");return 0;}
    wchar_t *candidate=allocate(32768*sizeof(*candidate));double total=0;int ok=1;
    for(int i=1;i<=count;i++){swprintf(candidate,32768,L"%.*ls-%05d-of-%05d.gguf",(int)(length-20),path,i,count);DWORD attributes=GetFileAttributesW(candidate);HANDLE f=CreateFileW(candidate,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);LARGE_INTEGER size={0};unsigned char header[8];DWORD read;
        ok=f!=INVALID_HANDLE_VALUE&&attributes!=INVALID_FILE_ATTRIBUTES&&!(attributes&FILE_ATTRIBUTE_REPARSE_POINT)&&GetFileSizeEx(f,&size)&&size.QuadPart>=24&&ReadFile(f,header,8,&read,NULL)&&read==8&&!memcmp(header,"GGUF",4)&&(header[4]==2||header[4]==3)&&!header[5]&&!header[6]&&!header[7];if(f!=INVALID_HANDLE_VALUE)CloseHandle(f);if(!ok)break;total+=(double)size.QuadPart;
    }free(candidate);free(path);if(ok)setnum(model,"size",total);else strcpy(error,"One or more GGUF shards are missing, invalid, or linked. Keep all parts in the same folder.");return ok;
}

int inspect_gguf(cJSON *model,HANDLE cancel,char error[512]){
    wchar_t *path=wide(str(model,"path"));HANDLE file=path?CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL):INVALID_HANDLE_VALUE;free(path);
    if(file==INVALID_HANDLE_VALUE){strcpy(error,"Model file is missing or unreadable.");return 0;}
    LARGE_INTEGER length;DWORD read=0;unsigned char *bytes=NULL;int ok=0;
    if(!GetFileSizeEx(file,&length)||length.QuadPart<24){strcpy(error,"File is too short to be a GGUF model.");goto done;}
    DWORD size=length.QuadPart>METADATA_LIMIT?METADATA_LIMIT:(DWORD)length.QuadPart;bytes=allocate(size);
    if(!ReadFile(file,bytes,size,&read,NULL)||read!=size||memcmp(bytes,"GGUF",4)){strcpy(error,"Unsupported file format. Direct inference requires GGUF; Safetensors, PyTorch and audio models need their own runtime.");goto done;}
    Reader r={bytes+4,bytes+size,0,cancel};uint64_t version,tensors,count;int gemma3Rms=0;
    if(!number(&r,4,&version)||(version!=2&&version!=3)||!number(&r,8,&tensors)||!number(&r,8,&count)||count>8192)goto malformed;
    setnum(model,"size",(double)length.QuadPart);setstr(model,"format","GGUF");setnum(model,"tensorCount",(double)tensors);
    for(uint64_t i=0;i<count;i++){
        char *key=NULL;uint64_t type;if(!text_value(&r,&key)||!number(&r,4,&type)){free(key);goto malformed;}
        const char *destination=!strcmp(key,"general.architecture")?"architecture":!strcmp(key,"general.name")?"ggufName":!strcmp(key,"general.type")?"ggufType":(!strcmp(key,"tokenizer.chat_template")||!strcmp(key,"tokenizer.chat_template.default"))?"embeddedTemplate":NULL;
        if(destination&&type==8){char *value=NULL;if(!text_value(&r,&value)){free(key);goto malformed;}if(!strcmp(destination,"embeddedTemplate")){cJSON_DeleteItemFromObjectCaseSensitive(model,destination);cJSON_AddBoolToObject(model,destination,*value!=0);}else setstr(model,destination,value);free(value);}
        else if(!strcmp(key,"gemma3.attention.layer_norm_rms_epsilon")&&type==6){uint64_t bits;if(!number(&r,4,&bits)){free(key);goto malformed;}uint32_t raw=(uint32_t)bits;float epsilon;memcpy(&epsilon,&raw,sizeof(epsilon));gemma3Rms=isfinite(epsilon)&&epsilon>0;}
        else if((!strcmp(key,"split.no")||!strcmp(key,"split.count"))&&(type==2||type==4||type==10)){uint64_t value;if(!number(&r,type==2?2:type==4?4:8,&value)){free(key);goto malformed;}setnum(model,!strcmp(key,"split.no")?"splitNo":"splitCount",(double)value);}
        else if(!skip_value(&r,(unsigned)type,0)){free(key);goto malformed;}free(key);
    }
    if(!tensors){strcpy(error,"GGUF contains no model tensors.");goto done;}
    if(!*str(model,"architecture")){strcpy(error,"GGUF has no text-model architecture. Audio conversions and incompatible GGUF files need a different runtime.");goto done;}
    if(!strcmp(str(model,"architecture"),"clip")||!strcmp(str(model,"ggufType"),"projector")){strcpy(error,"Vision projector files are not standalone text models.");goto done;}
    if(strstr(str(model,"architecture"),"whisper")){strcpy(error,"Speech models require an audio runtime; this application currently provides text chat.");goto done;}
    // Some Ollama Gemma 3 bundles omit this required llama.cpp field and also
    // use a different tensor layout. Supplying a guessed value cannot convert
    // the model. Preserve the source and require a compatible export instead.
    if(!strcmp(str(model,"architecture"),"gemma3")&&!gemma3Rms){strcpy(error,"Gemma 3 lacks valid RMS normalization metadata required by the local runner. Add a llama.cpp-compatible Gemma 3 GGUF, or use its original Ollama runtime.");goto done;}
    if(get(model,"splitCount")&&get(model,"splitCount")->valuedouble>1&&!split_files(model,error))goto done;
    ok=1;goto done;
malformed:strcpy(error,stopped(cancel)?"Model discovery stopped.":"GGUF metadata is malformed, unsupported, or exceeds the 32 MiB inspection limit.");
done:free(bytes);CloseHandle(file);return ok;
}

static void choose_template(cJSON *model){
    const char *format=str(model,"template");
    if(cJSON_IsTrue(get(model,"embeddedTemplate"))){setstr(model,"template","");setstr(model,"promptSource","Embedded GGUF chat template");}
    else if(*format&&legacy_template_supported(format)){setstr(model,"promptSource","Ollama text template");}
    else if(*str(model,"chatTemplate")){setstr(model,"template","");setstr(model,"promptSource","Selected llama.cpp chat template");}
    else if(strstr(format,"<|start_header_id|>")){setstr(model,"template","");setstr(model,"chatTemplate","llama3");setstr(model,"promptSource","Llama 3 chat template");}
    else if(strstr(format,"<start_of_turn>")){setstr(model,"template","");setstr(model,"chatTemplate","gemma");setstr(model,"promptSource","Gemma chat template");}
    else if(strstr(format,"<｜begin▁of▁sentence｜>")&&strstr(format,"Assistant:")){setstr(model,"template","");setstr(model,"chatTemplate","deepseek2");setstr(model,"promptSource","DeepSeek 2 chat template");}
    else if(*format){unavailable(model,"The stored chat template needs the original Ollama runtime. Choose Ollama-compatible server in Settings.");return;}
    else{setstr(model,"template","{{ .System }}\nUser: {{ .Prompt }}\nAssistant: {{ .Response }}");setstr(model,"promptSource","Plain User/Assistant completion; no embedded chat template");}
    available(model,"Ready to load. Actual architecture and memory support are checked by the runtime when used.");
}
static void add_model(Scan *scan,cJSON *model,int inspect){
    if(cJSON_GetArraySize(scan->models)>=CATALOG_LIMIT){cJSON_Delete(model);scan->failed=1;strcpy(scan->error,"Model scan exceeded the 512-model limit. Choose a smaller folder.");return;}
    cJSON *old;cJSON_ArrayForEach(old,scan->models)if(!strcmp(str(old,"name"),str(model,"name"))&&!_stricmp(str(old,"path"),str(model,"path"))){cJSON_Delete(model);return;}
    char error[512]={0};if(inspect){if(inspect_gguf(model,scan->cancel,error))choose_template(model);else unavailable(model,error);}
    char *json=cJSON_PrintUnformatted(model);size_t bytes=json?strlen(json):SPARK_LIMIT;free(json);if(bytes>SPARK_LIMIT-scan->metadataBytes){cJSON_Delete(model);scan->failed=1;strcpy(scan->error,"Model metadata exceeds 8 MiB. Choose a smaller library.");return;}scan->metadataBytes+=bytes;
    cJSON_ArrayForEach(old,scan->models)if(!strcmp(str(old,"name"),str(model,"name"))){if(cJSON_IsFalse(get(model,"supported"))){cJSON_Delete(model);return;}if(cJSON_IsFalse(get(old,"supported"))){cJSON_ReplaceItemViaPointer(scan->models,old,model);return;}}
    // Preserve aliases for the same weights, but make names unambiguous when
    // separate files happen to share a display name.
    unsigned duplicate=1;char *name=_strdup(str(model,"name"));int unique=0;while(!unique){unique=1;cJSON_ArrayForEach(old,scan->models)if(!strcmp(str(old,"name"),str(model,"name"))){char label[1024];snprintf(label,sizeof(label),"%s [%u]",name,++duplicate);setstr(model,"name",label);unique=0;break;}}free(name);
    cJSON_AddItemToArray(scan->models,model);
}
static wchar_t *join(const wchar_t *a,const wchar_t *b){size_t n=wcslen(a)+wcslen(b)+2;if(n>=32768)return NULL;wchar_t *out=allocate(n*sizeof(*out));swprintf(out,n,L"%ls\\%ls",a,b);return out;}
static int contained(HANDLE file,const wchar_t *root){wchar_t *actual=allocate(32768*sizeof(*actual));DWORD n=GetFinalPathNameByHandleW(file,actual,32768,FILE_NAME_NORMALIZED);size_t len=wcslen(root);int ok=n&&n<32768&&n>len&&!_wcsnicmp(actual,root,len)&&actual[len]==L'\\';free(actual);return ok;}
static char *read_small(const wchar_t *path,const wchar_t *root,uint64_t expected,const char *digest,char error[512]){
    HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);if(file==INVALID_HANDLE_VALUE){strcpy(error,"A model manifest or metadata blob is missing.");return NULL;}
    LARGE_INTEGER size;char *out=NULL;DWORD n;
    if(!contained(file,root)||!GetFileSizeEx(file,&size)||size.QuadPart<0||size.QuadPart>1024*1024||(expected!=UINT64_MAX&&(uint64_t)size.QuadPart!=expected)){strcpy(error,"Unsafe, incomplete or oversized model metadata.");goto done;}
    out=allocate((size_t)size.QuadPart+1);if(!ReadFile(file,out,(DWORD)size.QuadPart,&n,NULL)||n!=size.QuadPart||memchr(out,0,n)||!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,out,-1,NULL,0)){free(out);out=NULL;strcpy(error,"Invalid model metadata text.");goto done;}
    if(digest){BCRYPT_ALG_HANDLE algorithm=NULL;unsigned char hash[32];char hex[65]={0};int ok=BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,NULL,0)>=0&&BCryptHash(algorithm,NULL,0,(PUCHAR)out,n,hash,32)>=0;if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);if(ok)for(unsigned i=0;i<32;i++)snprintf(hex+i*2,3,"%02x",hash[i]);if(!ok||strcmp(hex,digest+7)){free(out);out=NULL;strcpy(error,"Model metadata does not match its SHA-256 digest.");}}
done:CloseHandle(file);return out;
}
static wchar_t *blob_path(const wchar_t *root,const cJSON *descriptor,char error[512]){
    const char *digest=str(descriptor,"digest");cJSON *size=get(descriptor,"size");if(strlen(digest)!=71||strncmp(digest,"sha256:",7)||!cJSON_IsNumber(size)||!isfinite(size->valuedouble)||size->valuedouble<0||size->valuedouble>9007199254740991.0||floor(size->valuedouble)!=size->valuedouble)goto invalid;
    for(unsigned i=7;i<71;i++)if(!((digest[i]>='a'&&digest[i]<='f')||(digest[i]>='0'&&digest[i]<='9')))goto invalid;
    wchar_t name[100];swprintf(name,100,L"blobs\\sha256-%hs",digest+7);return join(root,name);
invalid:strcpy(error,"Invalid model blob digest or size.");return NULL;
}
static cJSON *json_text(const char *text){return text?cJSON_ParseWithOpts(text,NULL,1):NULL;}
static void manifest(Scan *scan,const wchar_t *root,const wchar_t *canonical,const wchar_t *path,const wchar_t *relative){
    char error[512]={0};char *name=utf8(relative);for(char *p=name;*p;p++)if(*p=='\\')*p='/';char *last=strrchr(name,'/');if(!last){free(name);return;}*last=':';char *shortname=name;if(!strncmp(shortname,"registry.ollama.ai/",19))shortname+=19;if(!strncmp(shortname,"library/",8))shortname+=8;
    cJSON *model=cJSON_CreateObject();setstr(model,"name",shortname);setstr(model,"source","Ollama library");char *source=utf8(root);setstr(model,"sourceRoot",source);free(source);free(name);
    char *text=read_small(path,canonical,UINT64_MAX,NULL,error);cJSON *data=json_text(text);free(text);cJSON *layers=get(data,"layers"),*weight=NULL,*layer;int count=0;
    if(!cJSON_IsObject(data)||!cJSON_IsNumber(get(data,"schemaVersion"))||get(data,"schemaVersion")->valueint!=2||!cJSON_IsArray(layers)||cJSON_GetArraySize(layers)>256){if(!*error)strcpy(error,"Unsupported or malformed Ollama manifest.");goto fail;}
    cJSON_ArrayForEach(layer,layers){const char *type=str(layer,"mediaType"),*prefix="application/vnd.ollama.image.";if(strncmp(type,prefix,strlen(prefix))){strcpy(error,"Unsupported non-GGUF Ollama layer.");goto fail;}type+=strlen(prefix);
        if(!strcmp(type,"model")){weight=layer;count++;}else if(strcmp(type,"template")&&strcmp(type,"system")&&strcmp(type,"params")&&strcmp(type,"license")){snprintf(error,sizeof(error),"Unsupported Ollama layer: %.80s. Use the model's original runtime.",type);goto fail;}
    }
    if(count!=1){strcpy(error,"Expected one complete GGUF model layer. Tensor stores and adapters need the original runtime.");goto fail;}
    wchar_t *weights=blob_path(root,weight,error);if(!weights)goto fail;
    HANDLE file=CreateFileW(weights,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);LARGE_INTEGER size;int valid=file!=INVALID_HANDLE_VALUE&&contained(file,canonical)&&GetFileSizeEx(file,&size)&&(double)size.QuadPart==get(weight,"size")->valuedouble;if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);
    if(!valid){free(weights);strcpy(error,"Model weights are missing, incomplete, or outside the chosen library.");goto fail;}char *utf=utf8(weights);setstr(model,"path",utf);free(utf);free(weights);
    cJSON_ArrayForEach(layer,layers){const char *type=str(layer,"mediaType")+strlen("application/vnd.ollama.image.");if(!strcmp(type,"model")||!strcmp(type,"license"))continue;
        wchar_t *metadata=blob_path(root,layer,error);if(!metadata)goto fail;char *content=read_small(metadata,canonical,(uint64_t)get(layer,"size")->valuedouble,str(layer,"digest"),error);free(metadata);if(!content)goto fail;
        if(!strcmp(type,"params")){cJSON *params=json_text(content);if(!cJSON_IsObject(params)){free(content);cJSON_Delete(params);strcpy(error,"Invalid model sampler parameters.");goto fail;}cJSON_AddItemToObject(model,"parameters",params);}else setstr(model,type,content);free(content);
    }
    cJSON_Delete(data);add_model(scan,model,1);return;
fail:cJSON_Delete(data);unavailable(model,*error?error:"Cannot inspect model.");add_model(scan,model,0);
}
static void walk(Scan *scan,const wchar_t *root,const wchar_t *canonical,const wchar_t *directory,const wchar_t *relative,unsigned depth,int ollama){
    if(scan->failed||stopped(scan->cancel))return;if(depth>12){scan->failed=1;strcpy(scan->error,"Model folder nesting exceeds 12 levels.");return;}
    wchar_t *pattern=join(directory,L"*");if(!pattern){scan->failed=1;strcpy(scan->error,"Model folder path is too long.");return;}WIN32_FIND_DATAW entry;HANDLE search=FindFirstFileW(pattern,&entry);free(pattern);if(search==INVALID_HANDLE_VALUE)return;
    do{if(!wcscmp(entry.cFileName,L".")||!wcscmp(entry.cFileName,L".."))continue;if(++scan->entries>20000){scan->failed=1;strcpy(scan->error,"Model scan exceeds 20,000 entries. Choose a narrower folder.");break;}if(entry.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)continue;
        wchar_t *path=join(directory,entry.cFileName),*rel=*relative?join(relative,entry.cFileName):_wcsdup(entry.cFileName);if(!path||!rel){free(path);free(rel);scan->failed=1;strcpy(scan->error,"Model path exceeds the Windows path limit.");break;}
        if(entry.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)walk(scan,root,canonical,path,rel,depth+1,ollama);
        else if(ollama)manifest(scan,root,canonical,path,rel);
        else {const wchar_t *extension=wcsrchr(entry.cFileName,L'.');if(extension&&(!_wcsicmp(extension,L".gguf")||!_wcsicmp(extension,L".safetensors")||!_wcsicmp(extension,L".pt")||!_wcsicmp(extension,L".pth"))){cJSON *model=cJSON_CreateObject();char *p=utf8(path),*n=utf8(rel),*s=utf8(root);setstr(model,"name",n);setstr(model,"path",p);setstr(model,"source","Model folder");setstr(model,"sourceRoot",s);free(n);free(p);free(s);add_model(scan,model,1);}}
        free(path);free(rel);if(scan->failed||stopped(scan->cancel))break;
    }while(FindNextFileW(search,&entry));FindClose(search);
}
cJSON *discover_models(const cJSON *config,HANDLE cancel,char error[512]){
    error[0]=0;Scan scan={cJSON_CreateArray(),cancel,0,0,error};cJSON *model,*folder;
    cJSON_ArrayForEach(model,get(config,"models")){cJSON *copy=cJSON_Duplicate(model,1);setstr(copy,"source","Added file");add_model(&scan,copy,1);if(scan.failed)break;}
    cJSON_ArrayForEach(folder,get(config,"modelFolders")){if(scan.failed||stopped(cancel))break;if(!cJSON_IsString(folder))continue;wchar_t *root=wide(folder->valuestring);if(!root)continue;
        HANDLE handle=CreateFileW(root,FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,NULL);if(handle==INVALID_HANDLE_VALUE){free(root);continue;}
        wchar_t *canonical=allocate(32768*sizeof(*canonical));DWORD len=GetFinalPathNameByHandleW(handle,canonical,32768,FILE_NAME_NORMALIZED);CloseHandle(handle);if(!len||len>=32768){free(canonical);free(root);continue;}
        wchar_t *manifests=join(root,L"manifests"),*blobs=join(root,L"blobs");int ollama=manifests&&blobs&&(GetFileAttributesW(manifests)&FILE_ATTRIBUTE_DIRECTORY)&&(GetFileAttributesW(blobs)&FILE_ATTRIBUTE_DIRECTORY)&&GetFileAttributesW(manifests)!=INVALID_FILE_ATTRIBUTES&&GetFileAttributesW(blobs)!=INVALID_FILE_ATTRIBUTES;
        walk(&scan,root,canonical,ollama?manifests:root,L"",0,ollama);free(manifests);free(blobs);free(canonical);free(root);
    }
    if(stopped(cancel)){scan.failed=1;strcpy(error,"Model discovery stopped.");}if(scan.failed){cJSON_Delete(scan.models);return NULL;}
    cJSON *ordered=cJSON_CreateArray();for(int ready=1;ready>=0;ready--){cJSON *item=scan.models->child;while(item){cJSON *next=item->next;if((!cJSON_IsFalse(get(item,"supported")))==ready)cJSON_AddItemToArray(ordered,cJSON_DetachItemViaPointer(scan.models,item));item=next;}}cJSON_Delete(scan.models);return ordered;
}
