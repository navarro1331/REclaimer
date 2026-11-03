#define _CRT_SECURE_NO_WARNINGS
#include "spark.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <shellapi.h>

static int passed=0;
int catalog_tests(void);
int personality_tests(void);
int history_tests(void);
int memory_tests(void);
int backfill_tests(void);
int gabo_tests(void);
int background_tests(void);
int activity_tests(void);
int notification_tests(void);
int web_tests(void);
#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);return 1;}passed++; } while(0)
typedef struct { SOCKET listener; HANDLE stop; const char *body; int status,fragment,stall,early; char base[64],request[32768]; } Fixture;
static int send_all(SOCKET socket,const char *data,int bytes){while(bytes){int n=send(socket,data,bytes,0);if(n<=0)return 0;data+=n;bytes-=n;}return 1;}
static DWORD WINAPI serve(void *arg){
    Fixture *f=arg;fd_set readable;FD_ZERO(&readable);FD_SET(f->listener,&readable);struct timeval timeout={5,0};if(select(0,&readable,NULL,NULL,&timeout)<=0)return 0;
    SOCKET client=accept(f->listener,NULL,NULL);if(client==INVALID_SOCKET)return 0;DWORD ms=3000;setsockopt(client,SOL_SOCKET,SO_RCVTIMEO,(char*)&ms,sizeof(ms));setsockopt(client,SOL_SOCKET,SO_SNDTIMEO,(char*)&ms,sizeof(ms));
    char request[32768]={0};size_t n=0;char *end=NULL;while(n<sizeof(request)-1&&!end){int got=recv(client,request+n,(int)(sizeof(request)-1-n),0);if(got<=0)goto done;n+=(size_t)got;request[n]=0;end=strstr(request,"\r\n\r\n");}
    if(!end)goto done;char *length=strstr(request,"Content-Length:");if(length){size_t expected=(size_t)strtoul(length+15,NULL,10)+(size_t)(end+4-request);while(n<expected&&n<sizeof(request)-1){int got=recv(client,request+n,(int)(sizeof(request)-1-n),0);if(got<=0)goto done;n+=(size_t)got;}}
    memcpy(f->request,request,n);f->request[n]=0;
    if(f->stall){WaitForSingleObject(f->stop,3000);goto done;}
    char headers[512];snprintf(headers,sizeof(headers),"HTTP/1.1 %d Test\r\nContent-Length: %zu\r\nContent-Type: application/x-ndjson\r\nLocation: http://example.com/\r\nConnection: close\r\n\r\n",f->status,strlen(f->body));if(!send_all(client,headers,(int)strlen(headers)))goto done;
    if(f->fragment==2){const char *split=strchr(f->body,'\n')+1;send_all(client,f->body,(int)(split-f->body));f->early=WaitForSingleObject(f->stop,1500)==WAIT_OBJECT_0;send_all(client,split,(int)strlen(split));}
    else if(f->fragment){for(const char *p=f->body;*p;p++){if(!send_all(client,p,1))break;Sleep(1);}}else send_all(client,f->body,(int)strlen(f->body));
done:shutdown(client,SD_BOTH);closesocket(client);return 0;
}
static HANDLE start_fixture(Fixture *f,const char *body,int code,int fragment,int stall){
    memset(f,0,sizeof(*f));f->body=body;f->status=code;f->fragment=fragment;f->stall=stall;f->stop=CreateEventW(NULL,TRUE,FALSE,NULL);f->listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    struct sockaddr_in address={0};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);int len=sizeof(address);
    if(bind(f->listener,(struct sockaddr*)&address,sizeof(address))||getsockname(f->listener,(struct sockaddr*)&address,&len)||listen(f->listener,1))return NULL;
    snprintf(f->base,sizeof(f->base),"http://127.0.0.1:%hu",ntohs(address.sin_port));return CreateThread(NULL,0,serve,f,0,NULL);
}
static void finish_fixture(Fixture *f,HANDLE server){SetEvent(f->stop);WaitForSingleObject(server,6000);CloseHandle(server);closesocket(f->listener);CloseHandle(f->stop);}
static int collect(const char *text,void *context){return adds((Buffer*)context,text);}
static int observe_stream(const char *text,void *context){(void)text;SetEvent((HANDLE)context);return 1;}
static DWORD WINAPI cancel_soon(void *event){Sleep(100);SetEvent(event);return 0;}
static int http_tests(void){
    WSADATA sockets;CHECK(WSAStartup(MAKEWORD(2,2),&sockets)==0);char error[512]={0};Fixture f;Buffer response={0};HANDLE server=start_fixture(&f,"{\"models\":[{\"name\":\"fixture\"}]}",200,1,0);CHECK(server!=NULL);
    cJSON *config=cJSON_CreateObject();setstr(config,"mode","ollama");setstr(config,"api",f.base);cJSON *models=list_models(config,NULL,error);CHECK(models&&cJSON_GetArraySize(models)==1);CHECK(!strcmp(str(models->child,"name"),"fixture"));cJSON_Delete(models);finish_fixture(&f,server);
    cJSON *state=new_state();CHECK(select_model(state,"fixture",error));cJSON *t=get(state,"threads")->child,*m=cJSON_CreateObject();setstr(m,"role","user");setstr(m,"content","Hello");cJSON_AddItemToArray(get(t,"messages"),m);Runner runner={0};
    server=start_fixture(&f,"{\"message\":{\"content\":\"Snowman \xe2\x98\x83\"},\"done\":false}\r\n{\"message\":{\"content\":\"!\"},\"done\":true}",200,1,0);CHECK(server!=NULL);setstr(config,"api",f.base);CHECK(generate(&runner,config,state,t,NULL,collect,&response,error));CHECK(!strcmp(response.p,"Snowman \xe2\x98\x83!"));free(response.p);memset(&response,0,sizeof(response));finish_fixture(&f,server);
    server=start_fixture(&f,"{\"message\":{\"content\":\"first token\"},\"done\":false}\n{\"done\":true}\n",200,2,0);CHECK(server!=NULL);setstr(config,"api",f.base);CHECK(generate(&runner,config,state,t,NULL,observe_stream,f.stop,error));WaitForSingleObject(server,2000);CHECK(f.early);finish_fixture(&f,server);
    server=start_fixture(&f,"{\"message\":{\"content\":\"partial\"},\"done\":false}\n",200,0,0);CHECK(server!=NULL);setstr(config,"api",f.base);CHECK(!generate(&runner,config,state,t,NULL,collect,&response,error));CHECK(strstr(error,"completion record")!=NULL);CHECK(!strcmp(response.p,"partial"));free(response.p);memset(&response,0,sizeof(response));finish_fixture(&f,server);
    server=start_fixture(&f,"{\"error\":\"fixture failure\"}\n",200,0,0);CHECK(server!=NULL);setstr(config,"api",f.base);CHECK(!generate(&runner,config,state,t,NULL,collect,&response,error));finish_fixture(&f,server);
    server=start_fixture(&f,"{\"done\":true} trailing\n",200,0,0);CHECK(server!=NULL);setstr(config,"api",f.base);CHECK(!generate(&runner,config,state,t,NULL,collect,&response,error));finish_fixture(&f,server);
    server=start_fixture(&f,"{\"message\":{\"content\":\"\xff\"},\"done\":true}\n",200,0,0);CHECK(server!=NULL);setstr(config,"api",f.base);CHECK(!generate(&runner,config,state,t,NULL,collect,&response,error));finish_fixture(&f,server);
    server=start_fixture(&f,"{\"done\":true}\n",200,0,0);CHECK(server!=NULL);setstr(config,"api",f.base);CHECK(!generate(&runner,config,state,t,NULL,collect,&response,error));CHECK(strstr(error,"without text")!=NULL);finish_fixture(&f,server);
    server=start_fixture(&f,"private failure body",500,0,0);CHECK(server!=NULL);CHECK(!http_request(f.base,L"/api/tags",NULL,NULL,NULL,NULL,NULL,&response,2000,error));CHECK(strstr(error,"500")&&strstr(error,"private")==NULL);finish_fixture(&f,server);
    server=start_fixture(&f,"",302,0,0);CHECK(server!=NULL);CHECK(!http_request(f.base,L"/api/tags",NULL,NULL,NULL,NULL,NULL,&response,2000,error));CHECK(strstr(error,"302")!=NULL);finish_fixture(&f,server);
    HANDLE cancel=CreateEventW(NULL,TRUE,FALSE,NULL);server=start_fixture(&f,"",200,0,1);CHECK(server!=NULL);HANDLE timer=CreateThread(NULL,0,cancel_soon,cancel,0,NULL);ULONGLONG started=GetTickCount64();CHECK(!http_request(f.base,L"/api/tags",NULL,NULL,cancel,NULL,NULL,&response,5000,error));CHECK(strstr(error,"stopped")!=NULL);CHECK(GetTickCount64()-started<1500);finish_fixture(&f,server);WaitForSingleObject(timer,2000);CloseHandle(timer);CloseHandle(cancel);
    server=start_fixture(&f,"",200,0,1);CHECK(server!=NULL);started=GetTickCount64();CHECK(!http_request(f.base,L"/api/tags",NULL,NULL,NULL,NULL,NULL,&response,100,error));CHECK(GetTickCount64()-started<1500);CHECK(strstr(error,"timed out")!=NULL||strstr(error,"12002")!=NULL);finish_fixture(&f,server);
    free(response.p);memset(&response,0,sizeof(response));
    cJSON *guide=cJSON_CreateObject(),*coach=cJSON_CreateObject();setstr(guide,"id","guide");setstr(guide,"systemPrompt","PATIENT_GUIDE_ONLY");setstr(coach,"id","coach");setstr(coach,"systemPrompt","CODE_COACH_ONLY");cJSON_AddItemToArray(get(state,"characters"),guide);cJSON_AddItemToArray(get(state,"characters"),coach);
    CHECK(select_model(state,"model-a:latest",error));CHECK(select_character(state,"guide",error));CHECK(select_model(state,"model-b:latest",error));CHECK(select_character(state,"coach",error));
    const char *names[]={"model-a:latest","model-b:latest"},*expected[]={"PATIENT_GUIDE_ONLY","CODE_COACH_ONLY"};
    for(int i=0;i<2;i++){
        CHECK(select_model(state,names[i],error));t=find_id(get(state,"threads"),str(state,"activeThreadId"));m=cJSON_CreateObject();setstr(m,"role","user");setstr(m,"content",i?"HISTORY_B_ONLY":"HISTORY_A_ONLY");cJSON_AddItemToArray(get(t,"messages"),m);
        server=start_fixture(&f,"{\"message\":{\"content\":\"Received\"},\"done\":true}\n",200,0,0);CHECK(server!=NULL);setstr(config,"api",f.base);CHECK(generate(&runner,config,state,t,NULL,collect,&response,error));finish_fixture(&f,server);
        const char *body=strstr(f.request,"\r\n\r\n");CHECK(body!=NULL);cJSON *request=cJSON_Parse(body+4);CHECK(request!=NULL);CHECK(!strcmp(str(request,"model"),names[i]));cJSON *first=cJSON_GetArrayItem(get(request,"messages"),0);CHECK(!strcmp(str(first,"role"),"system"));CHECK(strstr(str(first,"content"),expected[i])!=NULL);CHECK(strstr(str(first,"content"),expected[1-i])==NULL);CHECK(strstr(body,i?"HISTORY_B_ONLY":"HISTORY_A_ONLY")!=NULL);CHECK(strstr(body,i?"HISTORY_A_ONLY":"HISTORY_B_ONLY")==NULL);cJSON_Delete(request);free(response.p);memset(&response,0,sizeof(response));
    }
    cJSON_Delete(state);cJSON_Delete(config);WSACleanup();return 0;
}
int main(void) {
    char err[512]={0};cJSON *s=new_state();CHECK(cJSON_GetArraySize(get(s,"threads"))==1);
    char *json=cJSON_PrintUnformatted(s);cJSON *round=import_state(json,NULL,err);CHECK(round!=NULL);CHECK(!strcmp(str(s,"activeThreadId"),str(round,"activeThreadId")));free(json);cJSON_Delete(round);
    const char *native="{\"threads\":[{\"id\":\"t\",\"name\":\"Imported\",\"lore\":\"Prior context\",\"createdAt\":1700000000000,\"messages\":[{\"role\":\"assistant\",\"content\":\"Welcome\"},{\"role\":\"user\",\"content\":\"Hello\"},{\"role\":\"user\",\"content\":\"More\"}]}],\"activeThreadId\":\"t\",\"characterCards\":[{\"id\":\"c\",\"name\":\"Guide\",\"writingStyle\":\"Be concise\",\"startingLore\":\"An observatory\",\"customCode\":\"NEVER_COPY_ME\"}],\"activeCardId\":\"c\",\"settings\":{\"apiKey\":\"NEVER_COPY_ME\"}}";
    round=import_state(native,NULL,err);CHECK(round!=NULL);CHECK(!strcmp(str(round,"activeCharacterId"),"c"));
    CHECK(!strcmp(str(cJSON_GetArrayItem(get(round,"characters"),0),"systemPrompt"),"Be concise"));
    CHECK(!strcmp(str(get(round,"threads")->child,"createdAt"),"2023-11-14T22:13:20.000Z"));
    json=cJSON_PrintUnformatted(round);CHECK(strstr(json,"NEVER_COPY_ME")==NULL);free(json);
    cJSON *messages=build_messages(round,get(round,"threads")->child,err);CHECK(messages!=NULL);CHECK(cJSON_GetArraySize(messages)==2);CHECK(strstr(str(messages->child,"content"),"Welcome")!=NULL);CHECK(strstr(str(messages->child,"content"),"Prior context")!=NULL);CHECK(!strcmp(str(messages->child->next,"content"),"Hello\n\nMore"));cJSON_Delete(messages);cJSON_Delete(round);
    round=import_state("{\"spec\":\"chara_card_v3\",\"data\":{\"name\":\"M\",\"first_mes\":\"Hi\",\"system_prompt\":\"Kind\"}}",s,err);CHECK(round!=NULL);CHECK(cJSON_GetArraySize(get(round,"threads"))==1);CHECK(!strcmp(str(get(round,"characters")->child,"greeting"),"Hi"));cJSON_Delete(round);
    CHECK(import_state("{\"threads\":[],\"threads\":[]}",NULL,err)==NULL);
    CHECK(import_state("{\"threads\":[{\"messages\":[{\"role\":\"tool\",\"content\":\"x\"}]}]}",NULL,err)==NULL);
    CHECK(import_state("{\"threads\":[{\"messages\":[{\"role\":\"assistant\",\"content\":\"x\",\"tool_calls\":[]}]}]}",NULL,err)==NULL);
    CHECK(import_state("{\"threads\":[],\"settings\":{\"temperature\":4}}",NULL,err)==NULL);
    CHECK(import_state("{\"threads\":[],\"version\":8}",NULL,err)==NULL);
    CHECK(import_state("{}",NULL,err)==NULL);
    CHECK(import_state("{\"name\":\"bad\\u0000text\"}",NULL,err)==NULL);
    CHECK(import_state("{\"name\":\"\xff\"}",NULL,err)==NULL);
    round=import_state("{\"name\":\"literal\\\\u0000\"}",NULL,err);CHECK(round!=NULL);cJSON_Delete(round);
    const wchar_t *args[]={L"C:\\space path\\engine.exe",L"plain",L"with spaces",L"quote\"and\\",L"",L"&literal%value",L"unicode \x03a9",NULL};
    wchar_t *line=argv_command_line(args);int count;wchar_t **decoded=CommandLineToArgvW(line,&count);CHECK(count==7);for(int i=0;i<count;i++)CHECK(!wcscmp(args[i],decoded[i]));LocalFree(decoded);free(line);
    wchar_t *w=wide("Hello \xe2\x98\x83");char *u=utf8(w);CHECK(!strcmp(u,"Hello \xe2\x98\x83"));free(w);free(u);
    wchar_t temp[MAX_PATH],file[MAX_PATH];GetTempPathW(MAX_PATH,temp);GetTempFileNameW(temp,L"gsp",0,file);DeleteFileW(file);
    CHECK(save_json(file,s,err));setstr(s,"persona","Second save");CHECK(save_json(file,s,err));round=load_json(file,err);CHECK(round!=NULL);CHECK(!strcmp(str(round,"persona"),"Second save"));cJSON_Delete(round);
    wchar_t backup[MAX_PATH+8];swprintf(backup,MAX_PATH+8,L"%ls.bak",file);round=load_json(backup,err);CHECK(round!=NULL);CHECK(!*str(round,"persona"));cJSON_Delete(round);DeleteFileW(file);DeleteFileW(backup);
    Buffer response={0};CHECK(!http_request("http://example.com",L"/",NULL,NULL,NULL,NULL,NULL,&response,1000,err));CHECK(strstr(err,"loopback")!=NULL);free(response.p);
    CHECK(http_tests()==0);
    int catalog=catalog_tests();CHECK(catalog>0);passed+=catalog;
    int personalities=personality_tests();CHECK(personalities>0);passed+=personalities;
    int history=history_tests();CHECK(history>0);passed+=history;
    int memories=memory_tests();CHECK(memories>0);passed+=memories;
    int backfill=backfill_tests();CHECK(backfill>0);passed+=backfill;
    int imports=gabo_tests();CHECK(imports>0);passed+=imports;
    int background=background_tests();CHECK(background>0);passed+=background;
    int activity=activity_tests();CHECK(activity>0);passed+=activity;
    int notifications=notification_tests();CHECK(notifications>0);passed+=notifications;
    int web=web_tests();CHECK(web>0);passed+=web;
    cJSON_Delete(s);printf("PASS %d native C assertions\n",passed);return 0;
}
