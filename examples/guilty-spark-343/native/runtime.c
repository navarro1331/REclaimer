#define _CRT_SECURE_NO_WARNINGS
#include "spark.h"
#include <winhttp.h>
#include <bcrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

typedef struct { HANDLE ready,closed; DWORD status,error,count; } HttpEvent;
static void CALLBACK http_event(HINTERNET handle,DWORD_PTR context,DWORD status,void *info,DWORD size) {
    (void)handle; HttpEvent *event=(HttpEvent*)context;if(!event)return;
    if(status==WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING){SetEvent(event->closed);return;}
    event->status=status;event->count=status==WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE?*(DWORD*)info:size;
    if(status==WINHTTP_CALLBACK_STATUS_REQUEST_ERROR)event->error=((WINHTTP_ASYNC_RESULT*)info)->dwError;
    SetEvent(event->ready);
}
static int wait_http(HttpEvent *event,HANDLE cancel,ULONGLONG deadline,char error[512]) {
    HANDLE handles[2]={event->ready,cancel};ULONGLONG now=GetTickCount64();
    if(now>=deadline){strcpy(error,"Local inference request timed out.");return 0;}
    DWORD result=WaitForMultipleObjects(cancel?2:1,handles,FALSE,(DWORD)(deadline-now));
    if(result!=WAIT_OBJECT_0){strcpy(error,result==WAIT_OBJECT_0+1?"Generation stopped.":"Local inference request timed out.");return 0;}
    if(event->error){snprintf(error,512,"Local HTTP connection failed (Windows %lu).",event->error);return 0;}return 1;
}
int http_request(const char *base,const wchar_t *route,const char *body,const char *key,HANDLE cancel,TextSink sink,void *context,Buffer *response,DWORD timeout,char error[512]) {
    int ok=0,context_set=0;wchar_t *url=wide(base);HINTERNET session=NULL,connection=NULL,request=NULL;
    HttpEvent *event=allocate(sizeof(*event));event->ready=CreateEventW(NULL,FALSE,FALSE,NULL);event->closed=CreateEventW(NULL,TRUE,FALSE,NULL);
    wchar_t host[256]={0},path[256]={0};URL_COMPONENTS parts={sizeof(parts)};parts.lpszHostName=host;parts.dwHostNameLength=255;parts.lpszUrlPath=path;parts.dwUrlPathLength=255;parts.dwUserNameLength=(DWORD)-1;parts.dwPasswordLength=(DWORD)-1;parts.dwExtraInfoLength=(DWORD)-1;
    if(!url||!WinHttpCrackUrl(url,0,0,&parts)||parts.nScheme!=INTERNET_SCHEME_HTTP||parts.dwUserNameLength||parts.dwPasswordLength||parts.dwExtraInfoLength||(*path&&wcscmp(path,L"/"))||(wcscmp(host,L"127.0.0.1")&&_wcsicmp(host,L"localhost")&&wcscmp(host,L"::1")&&wcscmp(host,L"[::1]"))){strcpy(error,"Use a loopback HTTP address without credentials, path or query.");goto done;}
    session=WinHttpOpen(L"GuiltySpark343/0.9",WINHTTP_ACCESS_TYPE_NO_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,WINHTTP_FLAG_ASYNC);if(!session)goto winerror;
    WinHttpSetTimeouts(session,5000,5000,(int)timeout,(int)timeout);
    connection=WinHttpConnect(session,host,parts.nPort,0);if(!connection)goto winerror;
    request=WinHttpOpenRequest(connection,body?L"POST":L"GET",route,NULL,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,0);if(!request)goto winerror;
    DWORD policy=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;WinHttpSetOption(request,WINHTTP_OPTION_REDIRECT_POLICY,&policy,sizeof(policy));
    DWORD_PTR ctx=(DWORD_PTR)event;if(!WinHttpSetOption(request,WINHTTP_OPTION_CONTEXT_VALUE,&ctx,sizeof(ctx)))goto winerror;
    if(WinHttpSetStatusCallback(request,http_event,WINHTTP_CALLBACK_FLAG_SENDREQUEST_COMPLETE|WINHTTP_CALLBACK_FLAG_HEADERS_AVAILABLE|WINHTTP_CALLBACK_FLAG_DATA_AVAILABLE|WINHTTP_CALLBACK_FLAG_READ_COMPLETE|WINHTTP_CALLBACK_FLAG_REQUEST_ERROR|WINHTTP_CALLBACK_FLAG_HANDLES,0)==WINHTTP_INVALID_STATUS_CALLBACK)goto winerror;
    context_set=1;ULONGLONG deadline=GetTickCount64()+timeout;
    wchar_t headers[256]=L"Content-Type: application/json\r\n";
    if(key&&*key){wchar_t *k=wide(key);swprintf(headers,256,L"Content-Type: application/json\r\nAuthorization: Bearer %ls\r\n",k);free(k);}
    if(body&&strlen(body)>1024*1024){strcpy(error,"Request exceeds 1 MiB. Start a new conversation.");goto done;}
    if(!WinHttpSendRequest(request,headers,(DWORD)-1,(void*)body,body?(DWORD)strlen(body):0,body?(DWORD)strlen(body):0,ctx))goto winerror;
    if(!wait_http(event,cancel,deadline,error))goto done;
    if(!WinHttpReceiveResponse(request,NULL))goto winerror;
    if(!wait_http(event,cancel,deadline,error))goto done;
    DWORD status=0,length=sizeof(status);if(!WinHttpQueryHeaders(request,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&length,WINHTTP_NO_HEADER_INDEX))goto winerror;
    if(status!=200){snprintf(error,512,"Local runtime returned HTTP %lu. Check the model and server settings.",status);goto done;}
    char block[8192];for(;;){
        if(cancel&&WaitForSingleObject(cancel,0)==WAIT_OBJECT_0){strcpy(error,"Generation stopped.");goto done;}
        // Query first: a fixed-size read may wait to fill its buffer, delaying
        // short token streams. Callbacks signal events, never recurse into reads.
        if(!WinHttpQueryDataAvailable(request,NULL))goto winerror;
        if(!wait_http(event,cancel,deadline,error))goto done;
        DWORD available=event->count,amount=available&&available<sizeof(block)?available:sizeof(block);
        if(!WinHttpReadData(request,block,amount,NULL))goto winerror;
        if(!wait_http(event,cancel,deadline,error))goto done;
        if(!event->count)break;
        if(sink){char *chunk=allocate((size_t)event->count+1);memcpy(chunk,block,event->count);int accepted=sink(chunk,context);free(chunk);if(!accepted){strcpy(error,"Stream was stopped or exceeded its size limit.");goto done;}}
        else if(response&&!append(response,block,event->count)){strcpy(error,"Runtime response exceeds 8 MiB.");goto done;}
    }ok=1;goto done;
winerror: snprintf(error,512,"Local HTTP operation failed (Windows %lu).",GetLastError());
done:
    if(request)WinHttpCloseHandle(request);
    // The asynchronous request owns callbacks until HANDLE_CLOSING. No API is
    // called on that request again, and its callback storage outlives closure.
    int closed=!context_set||WaitForSingleObject(event->closed,10000)==WAIT_OBJECT_0;
    if(connection)WinHttpCloseHandle(connection);if(session)WinHttpCloseHandle(session);
    if(closed){CloseHandle(event->ready);CloseHandle(event->closed);free(event);} // Preserve storage on a pathological OS callback delay.
    free(url);return ok;
}

wchar_t *argv_command_line(const wchar_t *const *argv) {
    wchar_t *out=allocate(32768*sizeof(wchar_t));size_t n=0;
    for(size_t arg=0;argv[arg];arg++) {
        if(arg)out[n++]=L' ';out[n++]=L'"';size_t slashes=0;
        for(const wchar_t *p=argv[arg];;p++) {
            if(*p==L'\\'){slashes++;continue;}
            size_t repeat=(*p==L'"'||!*p)?slashes*2:slashes;
            if(n+repeat+4>=32768){free(out);return NULL;}
            while(repeat--)out[n++]=L'\\';slashes=0;
            if(!*p)break;if(*p==L'"')out[n++]=L'\\';out[n++]=*p;
        }out[n++]=L'"';
    }out[n]=0;return out;
}
void runner_stop(Runner *r) {
    if(r->job){CloseHandle(r->job);r->job=NULL;}
    if(r->process){WaitForSingleObject(r->process,5000);CloseHandle(r->process);r->process=NULL;}
    free(r->model);r->model=NULL;free(r->chatTemplate);r->chatTemplate=NULL;r->port=0;SecureZeroMemory(r->key,sizeof(r->key));
}
static unsigned short free_port(void) {
    WSADATA data;if(WSAStartup(MAKEWORD(2,2),&data))return 0;SOCKET s=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);struct sockaddr_in addr={0};int len=sizeof(addr);unsigned short result=0;
    addr.sin_family=AF_INET;addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    if(s!=INVALID_SOCKET&&!bind(s,(struct sockaddr*)&addr,sizeof(addr))&&!getsockname(s,(struct sockaddr*)&addr,&len))result=ntohs(addr.sin_port);
    if(s!=INVALID_SOCKET)closesocket(s);WSACleanup();return result;
}
int runner_start(Runner *r,const cJSON *config,const cJSON *model,HANDLE cancel,char error[512]) {
    const char *modelpath=str(model,"path");if(r->process&&r->model&&!strcmp(r->model,modelpath)&&r->chatTemplate&&!strcmp(r->chatTemplate,str(model,"chatTemplate"))&&WaitForSingleObject(r->process,0)==WAIT_TIMEOUT)return 1;
    runner_stop(r);wchar_t *exe=wide(str(config,"llamaServer")),*weights=wide(modelpath);int ok=0;
    if(!exe||!*exe||GetFileAttributesW(exe)==INVALID_FILE_ATTRIBUTES){strcpy(error,"llama-server.exe was not found. Select the bundled local runner or an Ollama server in Settings.");goto done;}
    HANDLE f=weights?CreateFileW(weights,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL):INVALID_HANDLE_VALUE;char header[8];DWORD read=0;
    if(f==INVALID_HANDLE_VALUE||!ReadFile(f,header,8,&read,NULL)||read!=8||memcmp(header,"GGUF",4)||((unsigned char)header[4]!=2&&(unsigned char)header[4]!=3)||header[5]||header[6]||header[7]){if(f!=INVALID_HANDLE_VALUE)CloseHandle(f);strcpy(error,"Select an existing GGUF v2/v3 model file.");goto done;}CloseHandle(f);
    r->port=free_port();if(!r->port){strcpy(error,"Cannot allocate a local inference port.");goto done;}
    unsigned char random[32];if(BCryptGenRandom(NULL,random,sizeof(random),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0){strcpy(error,"Cannot create runtime authentication.");goto done;}
    for(unsigned i=0;i<32;i++)snprintf(r->key+i*2,3,"%02x",random[i]);SecureZeroMemory(random,sizeof(random));
    wchar_t port[16],context[16],threads[16],layers[16],alias[80];char id[64];newid(id);wchar_t *idw=wide(id),*key=wide(r->key);
    swprintf(alias,80,L"spark-%ls",idw);free(idw);swprintf(port,16,L"%hu",r->port);
    int ctx=get(config,"contextSize")?get(config,"contextSize")->valueint:2048,thr=get(config,"threads")?get(config,"threads")->valueint:4,gpu=get(config,"gpuLayers")?get(config,"gpuLayers")->valueint:0;
    if(ctx<128||ctx>131072||thr<1||thr>256||gpu<0||gpu>1000){free(key);strcpy(error,"Invalid context, CPU thread or GPU-layer configuration.");goto done;}
    swprintf(context,16,L"%d",ctx);swprintf(threads,16,L"%d",thr);swprintf(layers,16,L"%d",gpu);
    wchar_t *chatTemplate=wide(str(model,"chatTemplate"));
    const wchar_t *argv[40]={exe,L"--model",weights,L"--host",L"127.0.0.1",L"--port",port,L"--ctx-size",context,L"--threads",threads,L"--n-gpu-layers",layers,L"--parallel",L"1",L"--alias",alias,L"--api-key",key,L"--no-webui",L"--offline",L"--no-mmproj",L"--reasoning-format",L"none",NULL};
    if(chatTemplate&&*chatTemplate){argv[24]=L"--chat-template";argv[25]=chatTemplate;argv[26]=NULL;}
    int argumentCount=0;while(argv[argumentCount])argumentCount++;
    if(cJSON_IsTrue(get(model,"embedding"))){argv[argumentCount++]=L"--embeddings";argv[argumentCount++]=L"--pooling";argv[argumentCount++]=L"mean";argv[argumentCount]=NULL;}
    wchar_t *cacheType=wide(str(config,"kvCacheType"));
    if(cacheType&&(!wcscmp(cacheType,L"f16")||!wcscmp(cacheType,L"q8_0")||!wcscmp(cacheType,L"q4_0"))){argv[argumentCount++]=L"--cache-type-k";argv[argumentCount++]=cacheType;argv[argumentCount++]=L"--cache-type-v";argv[argumentCount++]=cacheType;argv[argumentCount]=NULL;}
    // argv is the launcher interface. Only this boundary serializes it for the
    // Windows process API; no shell or command interpreter is involved.
    wchar_t *command=argv_command_line(argv);free(chatTemplate);free(cacheType);if(!command){free(key);strcpy(error,"Runner arguments are too long.");goto done;}
    r->job=CreateJobObjectW(NULL,NULL);JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits={0};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if(!r->job||!SetInformationJobObject(r->job,JobObjectExtendedLimitInformation,&limits,sizeof(limits))){free(command);free(key);strcpy(error,"Cannot create inference process ownership.");goto done;}
    SECURITY_ATTRIBUTES sa={sizeof(sa),NULL,TRUE};HANDLE nullfile=CreateFileW(L"NUL",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,OPEN_EXISTING,0,NULL);
    STARTUPINFOW si={sizeof(si)};si.dwFlags=STARTF_USESHOWWINDOW|STARTF_USESTDHANDLES;si.wShowWindow=SW_HIDE;si.hStdInput=si.hStdOutput=si.hStdError=nullfile;PROCESS_INFORMATION pi={0};
    int started=CreateProcessW(exe,command,NULL,NULL,TRUE,CREATE_SUSPENDED|CREATE_NO_WINDOW,NULL,NULL,&si,&pi);SecureZeroMemory(command,wcslen(command)*sizeof(*command));free(command);SecureZeroMemory(key,wcslen(key)*sizeof(*key));free(key);CloseHandle(nullfile);
    if(!started){snprintf(error,512,"Cannot start local runner (Windows %lu).",GetLastError());goto done;}
    if(!AssignProcessToJobObject(r->job,pi.hProcess)){TerminateProcess(pi.hProcess,1);WaitForSingleObject(pi.hProcess,5000);CloseHandle(pi.hThread);CloseHandle(pi.hProcess);strcpy(error,"Cannot bind the inference process to this application.");goto done;}
    r->process=pi.hProcess;ResumeThread(pi.hThread);CloseHandle(pi.hThread);
    char base[128];snprintf(base,sizeof(base),"http://127.0.0.1:%hu",r->port);ULONGLONG deadline=GetTickCount64()+120000;
    while(GetTickCount64()<deadline) {
        if(cancel&&WaitForSingleObject(cancel,100)==WAIT_OBJECT_0){strcpy(error,"Generation stopped.");goto done;}
        if(WaitForSingleObject(r->process,0)!=WAIT_TIMEOUT){strcpy(error,"The local runner exited while loading. Check model architecture, available memory, and runtime DLLs.");goto done;}
        Buffer response={0};int ready=http_request(base,L"/v1/models",NULL,r->key,cancel,NULL,NULL,&response,1500,error);
        if(ready){cJSON *v=cJSON_Parse(response.p?response.p:""),*data=get(v,"data"),*first=cJSON_GetArrayItem(data,0);char *expected=utf8(alias);ready=!strcmp(str(first,"id"),expected);free(expected);cJSON_Delete(v);}
        free(response.p);if(ready){r->model=_strdup(modelpath);r->chatTemplate=_strdup(str(model,"chatTemplate"));ok=1;error[0]=0;break;}
    }
    if(!ok&&!*error)strcpy(error,"Model loading timed out.");
done: free(exe);free(weights);if(!ok)runner_stop(r);return ok;
}
cJSON *generation_parameters(const cJSON *body,int ollama,char error[512]){
    static const struct {const char *name;double min,max;int integer;} rules[]={
        {"top_k",0,10000,1},{"top_p",0,1,0},{"min_p",0,1,0},{"repeat_penalty",0,10,0},{"repeat_last_n",-1,1048576,1},
        {"seed",-1,4294967295.0,1},{"frequency_penalty",-2,2,0},{"presence_penalty",-2,2,0},
        {"xtc_probability",0,1,0},{"xtc_threshold",0,1,0},{"dry_multiplier",0,10,0},{"dry_base",1,10,0},{"dry_allowed_length",0,10000,1},{"dry_penalty_last_n",-1,1048576,1}};
    cJSON *out=cJSON_CreateObject();error[0]=0;
    for(size_t i=0;i<sizeof(rules)/sizeof(rules[0]);i++){cJSON *v=get(body,rules[i].name);if(!v)continue;
        if(!cJSON_IsNumber(v)||!isfinite(v->valuedouble)||v->valuedouble<rules[i].min||v->valuedouble>rules[i].max||(rules[i].integer&&floor(v->valuedouble)!=v->valuedouble)){snprintf(error,512,"Invalid sampling parameter: %s.",rules[i].name);goto fail;}
        if(ollama&&i>=8){if((i==8||i==10)&&v->valuedouble!=0){strcpy(error,"XTC and DRY sampling require a local GGUF runtime. Disable them for an Ollama connection.");goto fail;}continue;}
        cJSON_AddItemToObject(out,rules[i].name,cJSON_Duplicate(v,1));
    }
    cJSON *stop=get(body,"stop"),*v;if(stop){if(!cJSON_IsArray(stop)||cJSON_GetArraySize(stop)>32){strcpy(error,"Stop strings must be an array of at most 32 strings.");goto fail;}cJSON_ArrayForEach(v,stop)if(!cJSON_IsString(v)||strlen(v->valuestring)>200){strcpy(error,"Invalid stop string.");goto fail;}cJSON_AddItemToObject(out,"stop",cJSON_Duplicate(stop,1));}
    cJSON *bias=get(body,"logit_bias");if(bias){if(ollama){strcpy(error,"Token bias requires a local GGUF runtime.");goto fail;}if(!cJSON_IsObject(bias)||cJSON_GetArraySize(bias)>2048){strcpy(error,"Invalid token bias map.");goto fail;}
        cJSON_ArrayForEach(v,bias){char *end;unsigned long token=strtoul(v->string?v->string:"",&end,10);if(!v->string||!*v->string||*end||token>10000000||!cJSON_IsNumber(v)||v->valuedouble < -100||v->valuedouble>100){strcpy(error,"Invalid token bias entry.");goto fail;}}cJSON_AddItemToObject(out,"logit_bias",cJSON_Duplicate(bias,1));}
    return out;
fail:cJSON_Delete(out);return NULL;
}
typedef struct { Buffer line; int native,ollama,done,thinking; size_t total; TextSink sink,reasoning; void *context; } Stream;
static int parse_frame(Stream *s,const char *line) {
    if(!*line||!strcmp(line,"[DONE]"))return 1;if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,line,-1,NULL,0))return 0;cJSON *v=cJSON_ParseWithOpts(line,NULL,1);if(!cJSON_IsObject(v)){cJSON_Delete(v);return 0;}if(get(v,"error")){cJSON_Delete(v);return 0;}
    const char *text="",*thinking="";
    if(s->ollama){text=str(get(v,"message"),"content");thinking=str(get(v,"message"),"thinking");if(cJSON_IsTrue(get(v,"done")))s->done=1;}
    else if(s->native){text=str(v,"content");if(cJSON_IsTrue(get(v,"stop")))s->done=1;}
    else {cJSON *choice=cJSON_GetArrayItem(get(v,"choices"),0);text=str(get(choice,"delta"),"content");thinking=str(get(choice,"delta"),"reasoning_content");cJSON *reason=get(choice,"finish_reason");if(reason&&!cJSON_IsNull(reason))s->done=1;}
    s->total+=strlen(text)+strlen(thinking);int ok=s->total<=SPARK_TEXT;
    if(ok&&*thinking){if(s->reasoning)ok=s->reasoning(thinking,s->context);else{if(!s->thinking){ok=s->sink("<think>\n",s->context);s->thinking=1;}if(ok)ok=s->sink(thinking,s->context);}}
    if(ok&&s->thinking&&(*text||s->done)){ok=s->sink("\n</think>\n",s->context);s->thinking=0;}
    if(ok&&*text)ok=s->sink(text,s->context);cJSON_Delete(v);return ok;
}
static int stream_bytes(const char *data,void *context) {
    Stream *s=context;for(const char *p=data;*p;p++) {
        if(*p=='\n'){if(s->line.n&&s->line.p[s->line.n-1]=='\r')s->line.p[--s->line.n]=0;
            const char *line=s->line.p?s->line.p:"";int ok=1;if(s->ollama)ok=parse_frame(s,line);else if(!strncmp(line,"data:",5)){line+=5;while(*line==' ')line++;ok=parse_frame(s,line);}
            s->line.n=0;if(s->line.p)s->line.p[0]=0;if(!ok)return 0;
        }else if(s->line.n>=1024*1024||!append(&s->line,p,1))return 0;
    }return 1;
}
int generate_ex(Runner *runner,const cJSON *config,const cJSON *state,const cJSON *thread,HANDLE cancel,TextSink sink,TextSink reasoning,void *context,char error[512]) {
    error[0]=0;cJSON *messages=build_messages(state,thread,error);if(!messages)return 0;
    int ollama=!strcmp(str(config,"mode"),"ollama"),native=0,ok=0;char base[512];const wchar_t *route=L"/api/chat";
    cJSON *body=cJSON_CreateObject();cJSON_AddBoolToObject(body,"stream",1);const char *modelname=str(state,"selectedModel");cJSON *opts=get(state,"settings");
    if(ollama){snprintf(base,sizeof(base),"%s",str(config,"api"));setstr(body,"model",modelname);cJSON_AddItemToObject(body,"messages",messages);messages=NULL;cJSON *o=cJSON_AddObjectToObject(body,"options");setnum(o,"temperature",get(opts,"temperature")->valuedouble);setnum(o,"num_predict",get(opts,"numPredict")->valuedouble);}
    else {
        cJSON *model=NULL,*m;cJSON_ArrayForEach(m,get(config,"models"))if(!strcmp(str(m,"name"),modelname)){model=m;break;}
        if(!model){strcpy(error,"Select or add a local GGUF model.");goto done;}
        if(cJSON_IsFalse(get(model,"supported"))){snprintf(error,512,"%s",str(model,"reason"));goto done;}
        if(!runner_start(runner,config,model,cancel,error))goto done;snprintf(base,sizeof(base),"http://127.0.0.1:%hu",runner->port);
        if(*str(model,"system")&&strcmp(str(messages->child,"role"),"system")){cJSON *system=cJSON_CreateObject();setstr(system,"role","system");setstr(system,"content",str(model,"system"));cJSON_InsertItemInArray(messages,0,system);}
        const char *format=str(model,"template");native=*format!=0;
        if(native){Buffer prompt={0};
            if(!legacy_prompt(&prompt,format,messages,str(model,"system"),error)){free(prompt.p);goto done;}
            setstr(body,"prompt",prompt.p);free(prompt.p);route=L"/completion";setnum(body,"n_predict",get(opts,"numPredict")->valuedouble);
        }else {route=L"/v1/chat/completions";cJSON_AddItemToObject(body,"messages",messages);messages=NULL;setnum(body,"max_tokens",get(opts,"numPredict")->valuedouble);}
        cJSON *param;cJSON_ArrayForEach(param,get(model,"parameters"))if(param->string&&(!strcmp(param->string,"stop")||!strcmp(param->string,"top_k")||!strcmp(param->string,"top_p")||!strcmp(param->string,"min_p")||!strcmp(param->string,"repeat_penalty")||!strcmp(param->string,"repeat_last_n")))cJSON_AddItemToObject(body,param->string,cJSON_Duplicate(param,1));
        setnum(body,"temperature",get(opts,"temperature")->valuedouble);
    }
    cJSON *parameter,*target=ollama?get(body,"options"):body;cJSON_ArrayForEach(parameter,get(state,"generationParameters")){cJSON_DeleteItemFromObjectCaseSensitive(target,parameter->string);cJSON_AddItemToObject(target,parameter->string,cJSON_Duplicate(parameter,1));}
    char *payload=cJSON_PrintUnformatted(body);Stream stream={0};stream.ollama=ollama;stream.native=native;stream.sink=sink;stream.reasoning=reasoning;stream.context=context;
    ok=http_request(base,route,payload,ollama?NULL:runner->key,cancel,stream_bytes,&stream,NULL,300000,error);
    if(ok&&stream.line.n){if(ollama)ok=parse_frame(&stream,stream.line.p);else if(!strncmp(stream.line.p,"data:",5))ok=parse_frame(&stream,stream.line.p+5);}
    if(ok&&!stream.done){strcpy(error,"The stream ended without a completion record. Partial output was retained.");ok=0;}
    if(ok&&!stream.total){strcpy(error,"The model completed without text output.");ok=0;}
    free(payload);free(stream.line.p);
done:cJSON_Delete(messages);cJSON_Delete(body);if(cancel&&WaitForSingleObject(cancel,0)==WAIT_OBJECT_0){if(!ollama)runner_stop(runner);strcpy(error,"Generation stopped. Partial output was retained.");}return ok;
}
int generate(Runner *runner,const cJSON *config,const cJSON *state,const cJSON *thread,HANDLE cancel,TextSink sink,void *context,char error[512]){return generate_ex(runner,config,state,thread,cancel,sink,NULL,context,error);}
cJSON *list_models(const cJSON *config,HANDLE cancel,char error[512]) {
    if(strcmp(str(config,"mode"),"ollama"))return discover_models(config,cancel,error);
    Buffer response={0};cJSON *out=NULL;if(http_request(str(config,"api"),L"/api/tags",NULL,NULL,cancel,NULL,NULL,&response,10000,error)){cJSON *v=cJSON_Parse(response.p?response.p:"");if(cJSON_IsArray(get(v,"models")))out=cJSON_Duplicate(get(v,"models"),1);else strcpy(error,"Runtime returned an invalid model inventory.");cJSON_Delete(v);}free(response.p);return out;
}
