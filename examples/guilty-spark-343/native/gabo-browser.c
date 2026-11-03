#define _CRT_SECURE_NO_WARNINGS
#include "spark.h"
#include <winhttp.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Chrome opens only a disposable copy of this origin's IndexedDB. The user's
// browser, cookies, credentials, extensions and application scripts are unused.
typedef struct { HANDLE process,job;HINTERNET session,connection,socket;int sequence;HANDLE cancel;ULONGLONG deadline; } Browser;
static int stopped(Browser *b){return (b->cancel&&WaitForSingleObject(b->cancel,0)==WAIT_OBJECT_0)||GetTickCount64()>b->deadline;}
static int allowed_file(const wchar_t *name){const wchar_t *dot=wcsrchr(name,L'.');return !wcscmp(name,L"CURRENT")||!wcsncmp(name,L"MANIFEST-",9)||(dot&&(!_wcsicmp(dot,L".log")||!_wcsicmp(dot,L".ldb")||!_wcsicmp(dot,L".sst")));}
static int copy_database(const wchar_t *source,const wchar_t *dest,Browser *b,char error[512]){
    DWORD attr=GetFileAttributesW(source);if(attr==INVALID_FILE_ATTRIBUTES||(attr&FILE_ATTRIBUTE_REPARSE_POINT)||!(attr&FILE_ATTRIBUTE_DIRECTORY)){strcpy(error,"The configured browser's Gabo net database is unavailable.");return 0;}
    if(SHCreateDirectoryExW(NULL,dest,NULL)!=ERROR_SUCCESS){strcpy(error,"Cannot create the private Gabo net database snapshot.");return 0;}
    wchar_t pattern[32768];swprintf(pattern,32768,L"%ls\\*",source);WIN32_FIND_DATAW data;HANDLE find=FindFirstFileW(pattern,&data);if(find==INVALID_HANDLE_VALUE){strcpy(error,"Cannot enumerate the Gabo net database.");return 0;}
    cJSON *files=cJSON_CreateArray();ULONGLONG total=0;int ok=1,count=0;
    do{
        if(data.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)||!allowed_file(data.cFileName))continue;
        ULONGLONG size=((ULONGLONG)data.nFileSizeHigh<<32)|data.nFileSizeLow;total+=size;
        if(total>64u*1024u*1024u||++count>4096||stopped(b)){ok=0;break;}
        wchar_t from[32768],to[32768];swprintf(from,32768,L"%ls\\%ls",source,data.cFileName);swprintf(to,32768,L"%ls\\%ls",dest,data.cFileName);
        HANDLE in=CreateFileW(from,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,NULL);
        HANDLE out=CreateFileW(to,GENERIC_WRITE,0,NULL,CREATE_NEW,0,NULL);BY_HANDLE_FILE_INFORMATION before={0},after={0};
        if(in==INVALID_HANDLE_VALUE||out==INVALID_HANDLE_VALUE||!GetFileInformationByHandle(in,&before)||(before.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)){ok=0;}
        char buffer[32768];ULONGLONG copied=0;DWORD n=0,written=0;
        while(ok){if(!ReadFile(in,buffer,sizeof(buffer),&n,NULL)){ok=0;break;}if(!n)break;copied+=n;if(copied>size||stopped(b)||!WriteFile(out,buffer,n,&written,NULL)||written!=n){ok=0;break;}}
        if(ok&&(!GetFileInformationByHandle(in,&after)||copied!=size||memcmp(&before.ftLastWriteTime,&after.ftLastWriteTime,sizeof(FILETIME))||before.nFileSizeHigh!=after.nFileSizeHigh||before.nFileSizeLow!=after.nFileSizeLow))ok=0;
        if(in!=INVALID_HANDLE_VALUE)CloseHandle(in);if(out!=INVALID_HANDLE_VALUE)CloseHandle(out);
        cJSON *entry=cJSON_CreateObject();char *name=utf8(data.cFileName);setstr(entry,"name",name);free(name);setnum(entry,"size",(double)size);setnum(entry,"high",data.ftLastWriteTime.dwHighDateTime);setnum(entry,"low",data.ftLastWriteTime.dwLowDateTime);cJSON_AddItemToArray(files,entry);
        if(!ok)break;
    }while(FindNextFileW(find,&data));FindClose(find);
    // Check the entire file set again; rotation or concurrent writes defer sync.
    int checked=0;find=FindFirstFileW(pattern,&data);if(find==INVALID_HANDLE_VALUE)ok=0;
    else {do{if(data.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)||!allowed_file(data.cFileName))continue;char *name=utf8(data.cFileName);cJSON *entry=NULL,*e;cJSON_ArrayForEach(e,files)if(!strcmp(str(e,"name"),name)){entry=e;break;}free(name);checked++;
        if(!entry||get(entry,"size")->valuedouble!=(double)(((ULONGLONG)data.nFileSizeHigh<<32)|data.nFileSizeLow)||get(entry,"high")->valuedouble!=data.ftLastWriteTime.dwHighDateTime||get(entry,"low")->valuedouble!=data.ftLastWriteTime.dwLowDateTime)ok=0;
    }while(FindNextFileW(find,&data));FindClose(find);}
    cJSON_Delete(files);if(!count||checked!=count)ok=0;if(!ok)strcpy(error,"Gabo net's browser database changed or could not be copied. Sync will retry; no chats were changed.");return ok;
}
static void remove_snapshot(const wchar_t *path){
    // This function is only called with the just-created, UUID-named snapshot.
    wchar_t *pattern=allocate(32768*sizeof(wchar_t)),*child=allocate(32768*sizeof(wchar_t));swprintf(pattern,32768,L"%ls\\*",path);WIN32_FIND_DATAW data;HANDLE find=FindFirstFileW(pattern,&data);
    if(find!=INVALID_HANDLE_VALUE){do{if(!wcscmp(data.cFileName,L".")||!wcscmp(data.cFileName,L".."))continue;swprintf(child,32768,L"%ls\\%ls",path,data.cFileName);
        if(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY){if(!(data.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT))remove_snapshot(child);else RemoveDirectoryW(child);}else DeleteFileW(child);
    }while(FindNextFileW(find,&data));FindClose(find);}RemoveDirectoryW(path);free(pattern);free(child);
}
static int send_protocol(Browser *b,int id,const char *method,cJSON *params){cJSON *q=cJSON_CreateObject();setnum(q,"id",id);setstr(q,"method",method);if(params)cJSON_AddItemToObject(q,"params",params);char *json=cJSON_PrintUnformatted(q);DWORD result=WinHttpWebSocketSend(b->socket,WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,json,(DWORD)strlen(json));free(json);cJSON_Delete(q);return result==NO_ERROR;}
static cJSON *protocol(Browser *b,const char *method,cJSON *params,char error[512]){
    int id=++b->sequence;if(!send_protocol(b,id,method,params)){strcpy(error,"Cannot request the offline browser database.");return NULL;}
    while(!stopped(b)){Buffer message={0};DWORD received;WINHTTP_WEB_SOCKET_BUFFER_TYPE type;DWORD result;
        do{char bytes[32768];result=WinHttpWebSocketReceive(b->socket,bytes,sizeof(bytes),&received,&type);if(result!=NO_ERROR||type==WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE||!append(&message,bytes,received))break;}while(type==WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE&&!stopped(b));
        if(result!=NO_ERROR||type==WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE||stopped(b)){free(message.p);break;}
        cJSON *reply=message.p?cJSON_Parse(message.p):NULL;free(message.p);if(!reply)break;
        if(!strcmp(str(reply,"method"),"Fetch.requestPaused")){
            cJSON *p=cJSON_CreateObject();setstr(p,"requestId",str(get(reply,"params"),"requestId"));setnum(p,"responseCode",200);
            cJSON *headers=cJSON_AddArrayToObject(p,"responseHeaders"),*h=cJSON_CreateObject();setstr(h,"name","Content-Type");setstr(h,"value","text/html; charset=utf-8");cJSON_AddItemToArray(headers,h);
            // Empty, inert HTML. Every request is fulfilled locally.
            setstr(p,"body","PCFkb2N0eXBlIGh0bWw+PHRpdGxlPk9mZmxpbmUgR2FibyBuZXQgY29weTwvdGl0bGU+");
            if(!send_protocol(b,++b->sequence,"Fetch.fulfillRequest",p)){cJSON_Delete(reply);break;}
        }
        if(get(reply,"id")&&get(reply,"id")->valueint==id){if(get(reply,"error")){cJSON_Delete(reply);strcpy(error,"The offline browser rejected the Gabo net database request.");return NULL;}cJSON *out=cJSON_Duplicate(get(reply,"result"),1);cJSON_Delete(reply);return out;}
        cJSON_Delete(reply);
    }strcpy(error,"Offline Gabo net database read timed out or was cancelled.");return NULL;
}
static int connect_page(Browser *b,unsigned short port,const char *path,char error[512]){
    b->session=WinHttpOpen(L"GuiltySpark343/1.0",WINHTTP_ACCESS_TYPE_NO_PROXY,NULL,NULL,0);if(!b->session)return 0;WinHttpSetTimeouts(b->session,1000,1000,2000,2000);
    b->connection=WinHttpConnect(b->session,L"127.0.0.1",port,0);wchar_t *w=wide(path);HINTERNET request=WinHttpOpenRequest(b->connection,L"GET",w,NULL,NULL,NULL,0);free(w);if(!request)return 0;
    DWORD status=0,size=sizeof(status);int ok=WinHttpSetOption(request,WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET,NULL,0)&&WinHttpSendRequest(request,NULL,0,NULL,0,0,0)&&WinHttpReceiveResponse(request,NULL)&&WinHttpQueryHeaders(request,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,NULL,&status,&size,NULL)&&status==101;
    if(ok)b->socket=WinHttpWebSocketCompleteUpgrade(request,0);WinHttpCloseHandle(request);if(!b->socket){strcpy(error,"Cannot connect to the isolated browser.");return 0;}return 1;
}
static const char *read_expression=
"(async()=>{const list=await indexedDB.databases();if(!list.some(x=>x.name==='gobbonet-state'))throw Error('No Gabo net database');"
"const db=await new Promise((ok,no)=>{const r=indexedDB.open('gobbonet-state');r.onsuccess=()=>ok(r.result);r.onerror=()=>no(r.error);});"
"try{const read=n=>new Promise((ok,no)=>{if(!db.objectStoreNames.contains(n))return ok([]);const r=db.transaction(n,'readonly').objectStore(n).getAll();r.onsuccess=()=>ok(r.result);r.onerror=()=>no(r.error);});"
"const threads=await read('threads'),records=await read('telemetry');"
"return JSON.stringify({threads:threads.map(t=>({id:t.id,name:t.name,model:t.model,createdAt:t.createdAt,pinned:t.pinned,lore:t.lore,messages:(t.messages||[]).map(m=>({role:m.role,content:m.content,model:m.model,timestamp:m.timestamp}))})),"
"telemetry:records.map(t=>({thread_id:t.thread_id,model_id:t.model_id,ts:t.ts}))});}finally{db.close();}})()";
cJSON *gabo_browser_snapshot(const cJSON *config,const wchar_t *scratch,HANDLE cancel,char error[512]){
    error[0]=0;cJSON *out=NULL;Browser b={0};b.cancel=cancel;b.deadline=GetTickCount64()+30000;
    wchar_t *origin=wide(str(config,"origin")),*profile=wide(str(config,"profile")),*exe=wide(str(config,"browser"));
    wchar_t *root=allocate(32768*sizeof(wchar_t)),*database=allocate(32768*sizeof(wchar_t)),*from=allocate(32768*sizeof(wchar_t));int created=0;
    URL_COMPONENTS url={sizeof(url)};wchar_t host[512],scheme[16],path[2048],user[512];url.lpszHostName=host;url.dwHostNameLength=512;url.lpszScheme=scheme;url.dwSchemeLength=16;url.lpszUrlPath=path;url.dwUrlPathLength=2048;url.lpszUserName=user;url.dwUserNameLength=512;
    if(!origin||!profile||!exe||!WinHttpCrackUrl(origin,0,0,&url)||(url.nScheme!=INTERNET_SCHEME_HTTP&&url.nScheme!=INTERNET_SCHEME_HTTPS)||url.dwUserNameLength||(wcscmp(path,L"/")&&*path)||wcschr(host,L'/')||wcschr(host,L'\\')||wcschr(host,L':')){strcpy(error,"Configure a browser executable, profile folder and plain Gabo net origin URL.");goto done;}
    char id[64];newid(id);wchar_t *uuid=wide(id);swprintf(root,32768,L"%ls\\gabo-snapshot-%ls",scratch,uuid);free(uuid);
    if(CreateDirectoryW(root,NULL)){created=1;}else{strcpy(error,"Cannot create a Gabo net snapshot directory.");goto done;}
    swprintf(from,32768,L"%ls\\IndexedDB\\%ls_%ls_%u.indexeddb.leveldb",profile,scheme,host,url.nPort);
    swprintf(database,32768,L"%ls\\Default\\IndexedDB\\%ls_%ls_%u.indexeddb.leveldb",root,scheme,host,url.nPort);
    if(!copy_database(from,database,&b,error))goto done;
    wchar_t userdir[32768];swprintf(userdir,32768,L"--user-data-dir=%ls",root);
    const wchar_t *argv[]={exe,L"--headless=new",L"--no-first-run",L"--no-default-browser-check",L"--disable-background-networking",L"--disable-sync",L"--disable-component-update",L"--disable-extensions",L"--disable-features=OptimizationHints,MediaRouter",L"--remote-debugging-port=0",L"--remote-debugging-address=127.0.0.1",L"--host-resolver-rules=MAP * ~NOTFOUND",L"--proxy-server=http://127.0.0.1:9",userdir,L"about:blank",NULL};
    wchar_t *command=argv_command_line(argv);STARTUPINFOW startup={sizeof(startup)};startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;PROCESS_INFORMATION pi={0};
    b.job=CreateJobObjectW(NULL,NULL);JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits={0};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if(!b.job||!SetInformationJobObject(b.job,JobObjectExtendedLimitInformation,&limits,sizeof(limits))||!CreateProcessW(exe,command,NULL,NULL,FALSE,CREATE_SUSPENDED|CREATE_NO_WINDOW,NULL,NULL,&startup,&pi)){free(command);strcpy(error,"Cannot start the configured browser for offline Gabo net import.");goto done;}free(command);b.process=pi.hProcess;
    if(!AssignProcessToJobObject(b.job,b.process)){TerminateProcess(b.process,1);CloseHandle(pi.hThread);strcpy(error,"Cannot own the isolated Gabo net browser process.");goto done;}ResumeThread(pi.hThread);CloseHandle(pi.hThread);
    wchar_t portFile[32768];swprintf(portFile,32768,L"%ls\\DevToolsActivePort",root);unsigned short port=0;
    while(!stopped(&b)&&WaitForSingleObject(b.process,0)==WAIT_TIMEOUT){HANDLE file=CreateFileW(portFile,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);if(file!=INVALID_HANDLE_VALUE){char text[256]={0};DWORD n;int ok=ReadFile(file,text,255,&n,NULL);CloseHandle(file);long number=ok?strtol(text,NULL,10):0;if(number>0&&number<=65535){port=(unsigned short)number;break;}}if(cancel)WaitForSingleObject(cancel,50);else Sleep(50);}
    if(!port){strcpy(error,"The isolated Gabo net browser did not start in time.");goto done;}
    char base[128];snprintf(base,sizeof(base),"http://127.0.0.1:%u",port);Buffer response={0};
    if(!http_request(base,L"/json/list",NULL,NULL,cancel,NULL,NULL,&response,3000,error)){free(response.p);goto done;}cJSON *targets=cJSON_Parse(response.p);free(response.p);const char *socket=NULL;cJSON *target;
    cJSON_ArrayForEach(target,targets)if(!strcmp(str(target,"type"),"page")){socket=str(target,"webSocketDebuggerUrl");break;}
    const char *wsPath=socket?strchr(socket+5,'/'):NULL;int connected=wsPath&&connect_page(&b,port,wsPath,error);cJSON_Delete(targets);if(!connected)goto done;
    cJSON *params=cJSON_CreateObject();cJSON *patterns=cJSON_AddArrayToObject(params,"patterns"),*pattern=cJSON_CreateObject();setstr(pattern,"urlPattern","*");cJSON_AddItemToArray(patterns,pattern);cJSON *reply=protocol(&b,"Fetch.enable",params,error);if(!reply)goto done;cJSON_Delete(reply);
    reply=protocol(&b,"Network.setBypassServiceWorker",cJSON_Parse("{\"bypass\":true}"),error);if(!reply)goto done;cJSON_Delete(reply);
    params=cJSON_CreateObject();setstr(params,"url",str(config,"origin"));reply=protocol(&b,"Page.navigate",params,error);if(!reply)goto done;if(*str(reply,"errorText")){cJSON_Delete(reply);strcpy(error,"Cannot establish the offline Gabo net origin.");goto done;}cJSON_Delete(reply);
    // Wait for the navigation's execution context, using only our inert page.
    for(int tries=0;tries<20&&!stopped(&b);tries++){params=cJSON_CreateObject();setstr(params,"expression","location.href");reply=protocol(&b,"Runtime.evaluate",params,error);int ready=reply&&!strcmp(str(get(reply,"result"),"value"),str(config,"origin"));cJSON_Delete(reply);if(ready)break;if(cancel)WaitForSingleObject(cancel,50);else Sleep(50);}
    params=cJSON_CreateObject();setstr(params,"expression",read_expression);cJSON_AddBoolToObject(params,"awaitPromise",1);cJSON_AddBoolToObject(params,"returnByValue",1);setnum(params,"timeout",10000);
    reply=protocol(&b,"Runtime.evaluate",params,error);if(reply){if(get(reply,"exceptionDetails"))strcpy(error,"The Gabo net database is unavailable in this browser profile. No chats were changed.");else out=parse_json(str(get(reply,"result"),"value"),error);cJSON_Delete(reply);}
done:
    if(b.socket)WinHttpCloseHandle(b.socket);if(b.connection)WinHttpCloseHandle(b.connection);if(b.session)WinHttpCloseHandle(b.session);
    if(b.job){TerminateJobObject(b.job,0);CloseHandle(b.job);}if(b.process){WaitForSingleObject(b.process,3000);CloseHandle(b.process);}
    if(created)remove_snapshot(root);free(root);free(database);free(from);free(origin);free(profile);free(exe);if(!out&&!*error)strcpy(error,"Cannot read the configured Gabo net browser source.");return out;
}
