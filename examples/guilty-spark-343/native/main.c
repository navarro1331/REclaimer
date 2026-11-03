#define _CRT_SECURE_NO_WARNINGS
#include "spark.h"
#include <commctrl.h>
#include <commdlg.h>
#include <richedit.h>
#include <shellapi.h>
#include <shlobj.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { ID_THREADS=100,ID_NEW,ID_RENAME,ID_DELETE,ID_PIN,ID_IMPORT,ID_EXPORT,ID_MODELS,ID_REFRESH,ID_ADD_MODEL,ID_CHARACTERS,ID_EDIT_CHARACTER,ID_SETTINGS,ID_TRANSCRIPT,ID_PROMPT,ID_SEND,ID_STOP,ID_STATUS,ID_ADD_FOLDER,ID_MODEL_INFO,ID_MEMORIES,ID_SYNC,ID_WEB,ID_PHONE,ID_ACTIVITY,ID_EDITOR_SAVE=200,ID_EDITOR_CANCEL,ID_EDITOR_NEW,ID_EDITOR_DELETE,ID_MEMORY_CLEAR,ID_MEMORY_TEXT,ID_MEMORY_ENABLED,ID_MEMORY_AUTO,ID_MEMORY_LEARN,ID_MEMORY_SCAN,ID_SYNC_NOW };
#define WM_DELTA (WM_APP+1)
#define WM_FINISHED (WM_APP+2)
#define WM_INITIALIZE (WM_APP+3)
#define WM_SYNC_READY (WM_APP+4)
#define WM_TRAY (WM_APP+5)
enum { ID_TRAY_OPEN=300,ID_TRAY_SYNC,ID_TRAY_SETTINGS,ID_TRAY_BACKGROUND,ID_TRAY_STARTUP,ID_TRAY_EXIT,ID_TRAY_ACTIVITY,ID_TRAY_NOTIFICATIONS };
enum { ID_ACTIVITY_MODELS=400,ID_ACTIVITY_TEXT,ID_ACTIVITY_SYNC,ID_ACTIVITY_NOTIFY,ID_ACTIVITY_TEST_NOTICE };
static const COLORREF BACK=RGB(12,19,30),PANEL=RGB(20,32,48),TEXT=RGB(225,236,245),CYAN=RGB(105,223,236);
typedef struct { int kind,ok,memoryChanged; cJSON *config,*state,*thread,*models; char error[512],memoryError[512]; } Work;
typedef struct { int kind; HWND fields[9]; char id[128]; } Editor;
typedef struct { cJSON *config,*source; char error[512]; } SyncWork;
static struct {
    HINSTANCE instance; HWND window,controls[32]; HFONT font,titlefont,smallfont; HBRUSH background,panel;
    cJSON *state,*config,*models,*reply,*replyThread; Runner runner; HANDLE worker,cancel,lock;
    wchar_t directory[32768],statePath[32768],configPath[32768],receiptPath[32768];
    char *requestedModel;
    HANDLE syncWorker,syncCancel; SyncWork *syncReady; char syncStatus[512]; ULONGLONG nextSync;
    SparkWeb *web;int webBusy;
    HANDLE openEvent;UINT taskbarCreated;int startHidden,keepRunning,forceExit,trayAdded,startupAction;
    char lastSyncUtc[32];int syncChecks,syncSucceeded;HWND activity; cJSON *syncCheck;
    cJSON *notices,*noticeActive,*noticeLast;ULONGLONG noticeRetry,noticeStarted;int noticeSubmitted,noticeShown,noticeWasShown,noticeOpen;
    char noticeError[180];
#ifdef SPARK_TEST_HARNESS
    int webTest,explicitData;
#endif
    int closing,smoke,smokeStarted,smokeAction,smokeActionSent,exitCode,dpi,listOnly,syncOnce;
} app;
static HWND control(int id){return app.controls[id-ID_THREADS];}
static int scaled(int value){return MulDiv(value,app.dpi,96);}
static void caption(HWND h,const char *text){wchar_t *w=wide(text);SetWindowTextW(h,w?w:L"Invalid UTF-8");free(w);}
static char *input(HWND h){int n=GetWindowTextLengthW(h);wchar_t *w=allocate(((size_t)n+1)*sizeof(*w));GetWindowTextW(h,w,n+1);char *s=utf8(w);free(w);return s?s:_strdup("");}
static void status(const char *text){caption(control(ID_STATUS),text);}
static void label(HDC dc,int x,int y,const wchar_t *text){TextOutW(dc,x,y,text,(int)wcslen(text));}
static void errorbox(HWND owner,const char *text){wchar_t *w=wide(text);MessageBoxW(owner,w?w:L"Operation failed.",L"Guilty Spark 343",MB_ICONERROR);free(w);}
static cJSON *thread(void){return find_id(get(app.state,"threads"),str(app.state,"activeThreadId"));}
static int remember_draft(void){char *text=input(control(ID_PROMPT));if(strlen(text)>SPARK_TEXT){free(text);status("Draft exceeds 50,000 UTF-8 bytes. Shorten it before changing conversations.");return 0;}setstr(thread(),"draft",text);free(text);return 1;}
static void render_draft(void){caption(control(ID_PROMPT),str(thread(),"draft"));}
static int message_total(void){int total=0;cJSON *t;cJSON_ArrayForEach(t,get(app.state,"threads"))total+=cJSON_GetArraySize(get(t,"messages"));return total;}
static int save(void){char error[512]={0};int ok=save_json(app.statePath,app.state,error);if(!ok)status(error);return ok;}
static void activity_refresh(void);
static void activity_open(void);
static void notification_tick(void);
static void notification_toggle(void);
static void notification_callback(UINT event);
static void check_load(void){
    wchar_t *path=allocate(32768*sizeof(wchar_t));char error[512]={0};swprintf(path,32768,L"%ls\\gabo-check.json",app.directory);cJSON *saved=load_json(path,error);int legacy=0;
    if(!saved){swprintf(path,32768,L"%ls\\background-status.json",app.directory);saved=load_json(path,error);legacy=1;}
    app.syncCheck=cJSON_CreateObject();if(saved&&activity_time_valid(str(saved,"lastCheckUtc"))){
        int ok=cJSON_IsTrue(get(saved,legacy?"lastCheckSucceeded":"succeeded"));setstr(app.syncCheck,"lastCheckUtc",str(saved,"lastCheckUtc"));cJSON_AddBoolToObject(app.syncCheck,"succeeded",ok);
        if(ok||activity_time_valid(str(saved,"lastSuccessfulCheckUtc")))setstr(app.syncCheck,"lastSuccessfulCheckUtc",ok?str(saved,"lastCheckUtc"):str(saved,"lastSuccessfulCheckUtc"));
        setstr(app.syncCheck,"summary",legacy?(ok?"Successful check recorded by an earlier version.":"An earlier check failed; use Check now to retry."):str(saved,"summary"));
        snprintf(app.lastSyncUtc,sizeof(app.lastSyncUtc),"%s",str(app.syncCheck,"lastCheckUtc"));app.syncSucceeded=ok;snprintf(app.syncStatus,sizeof(app.syncStatus),"%.511s",str(app.syncCheck,"summary"));
    }cJSON_Delete(saved);free(path);
}
static void check_complete(void){
    spark_utc(app.lastSyncUtc);app.syncChecks++;setstr(app.syncCheck,"lastCheckUtc",app.lastSyncUtc);if(app.syncSucceeded)setstr(app.syncCheck,"lastSuccessfulCheckUtc",app.lastSyncUtc);
    cJSON_DeleteItemFromObjectCaseSensitive(app.syncCheck,"succeeded");cJSON_AddBoolToObject(app.syncCheck,"succeeded",app.syncSucceeded);setstr(app.syncCheck,"summary",app.syncStatus);
    wchar_t *path=allocate(32768*sizeof(wchar_t));swprintf(path,32768,L"%ls\\gabo-check.json",app.directory);char error[512]={0};if(!save_json(path,app.syncCheck,error)){
        size_t n=strlen(app.syncStatus);snprintf(app.syncStatus+n,sizeof(app.syncStatus)-n," Check status could not be saved for restart.");setstr(app.syncCheck,"summary",app.syncStatus);
    }free(path);
}
static void background_report(void){
    cJSON *report=cJSON_CreateObject();setnum(report,"processId",GetCurrentProcessId());cJSON_AddBoolToObject(report,"running",!app.closing);cJSON_AddBoolToObject(report,"backgroundEnabled",app.keepRunning);cJSON_AddBoolToObject(report,"windowVisible",IsWindowVisible(app.window));cJSON_AddBoolToObject(report,"trayAvailable",app.trayAdded);cJSON_AddBoolToObject(report,"automaticImportEnabled",cJSON_IsTrue(get(get(app.config,"gaboSync"),"enabled")));
    setnum(report,"completedChecks",app.syncChecks);setstr(report,"lastCheckUtc",app.lastSyncUtc);cJSON_AddBoolToObject(report,"lastCheckSucceeded",app.syncSucceeded);setstr(report,"summary",app.syncStatus);setstr(report,"lastSuccessfulCheckUtc",str(app.syncCheck,"lastSuccessfulCheckUtc"));cJSON *events=get(app.state,"gaboActivity");setstr(report,"lastImportUtc",str(events?events->child:NULL,"at"));
    cJSON_AddBoolToObject(report,"importNotificationsEnabled",import_notifications_enabled(app.config));setnum(report,"notificationsPending",cJSON_GetArraySize(app.notices));setnum(report,"notificationsSubmitted",app.noticeSubmitted);setnum(report,"notificationsShown",app.noticeShown);setstr(report,"notificationError",app.noticeError);if(app.noticeLast)cJSON_AddItemToObject(report,"lastNotification",cJSON_Duplicate(app.noticeLast,1));
    wchar_t *path=allocate(32768*sizeof(wchar_t));swprintf(path,32768,L"%ls\\background-status.json",app.directory);char error[512]={0};if(!save_json(path,report,error))status("Cannot save background status; open Gabo net sync to review the latest check.");free(path);cJSON_Delete(report);
}
static void tray_update(int remove){
    NOTIFYICONDATAW icon={sizeof(icon)};icon.hWnd=app.window;icon.uID=343;
    if(remove){if(app.trayAdded)Shell_NotifyIconW(NIM_DELETE,&icon);app.trayAdded=0;return;}
    icon.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP|NIF_SHOWTIP;icon.uCallbackMessage=WM_TRAY;icon.hIcon=LoadIconW(NULL,IDI_APPLICATION);wcscpy(icon.szTip,L"Guilty Spark 343 - background importing");
    int ready=Shell_NotifyIconW(app.trayAdded?NIM_MODIFY:NIM_ADD,&icon);if(!ready&&!app.trayAdded)ready=Shell_NotifyIconW(NIM_MODIFY,&icon);if(ready){app.trayAdded=1;icon.uVersion=NOTIFYICON_VERSION_4;Shell_NotifyIconW(NIM_SETVERSION,&icon);}
}
static void show_main(void){if(app.closing)return;ShowWindow(app.window,IsIconic(app.window)?SW_RESTORE:SW_SHOW);if(!IsWindowVisible(app.window))ShowWindow(app.window,SW_SHOW);if(app.activity)ShowWindow(app.activity,SW_SHOW);SetForegroundWindow(GetLastActivePopup(app.window));background_report();}
static int hide_main(void){if(!remember_draft()||!save())return 0;if(!app.trayAdded)tray_update(0);if(!app.trayAdded){status("The Windows tray is unavailable; this window will stay open so you can reach 343.");return 0;}if(app.activity)ShowWindow(app.activity,SW_HIDE);ShowWindow(app.window,SW_HIDE);background_report();return 1;}
static void tray_menu(void){
    HMENU menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,ID_TRAY_OPEN,L"Open Guilty Spark 343");AppendMenuW(menu,MF_STRING,ID_TRAY_ACTIVITY,L"Import activity");AppendMenuW(menu,MF_STRING,ID_TRAY_SYNC,L"Import Gabo net now");AppendMenuW(menu,MF_STRING,ID_TRAY_SETTINGS,L"Gabo net settings");AppendMenuW(menu,MF_SEPARATOR,0,NULL);
    AppendMenuW(menu,MF_STRING|(import_notifications_enabled(app.config)?MF_CHECKED:0),ID_TRAY_NOTIFICATIONS,L"Notify about new chats and memories");
    AppendMenuW(menu,MF_STRING|(app.keepRunning?MF_CHECKED:0),ID_TRAY_BACKGROUND,L"Keep running when the window closes");
    wchar_t *path=allocate(32768*sizeof(wchar_t));char error[512]={0};int enabled=startup_path(path,error)?startup_status(path,app.directory,error):-1;free(path);
    AppendMenuW(menu,MF_STRING|(enabled==1?MF_CHECKED:0),ID_TRAY_STARTUP,L"Start with Windows");AppendMenuW(menu,MF_SEPARATOR,0,NULL);AppendMenuW(menu,MF_STRING,ID_TRAY_EXIT,L"Exit 343");
    POINT point;GetCursorPos(&point);SetForegroundWindow(app.window);int chosen=TrackPopupMenu(menu,TPM_RIGHTBUTTON|TPM_RETURNCMD,point.x,point.y,0,app.window,NULL);DestroyMenu(menu);PostMessageW(app.window,WM_NULL,0,0);if(chosen)PostMessageW(app.window,WM_COMMAND,chosen,0);
}
static void render_memories(void){char label[64];if(memory_option(app.state,"enabled"))snprintf(label,sizeof(label),"Memories (%d)",cJSON_GetArraySize(memory_notes(app.state)));else strcpy(label,"Memories off");caption(control(ID_MEMORIES),label);EnableWindow(control(ID_MEMORIES),!app.worker&&*str(app.state,"selectedModel"));}
static void save_config(void){char error[512]={0};if(!save_json(app.configPath,app.config,error))status(error);}
static cJSON *selected_model(void){cJSON *m;cJSON_ArrayForEach(m,app.models)if(!strcmp(str(m,"name"),str(app.state,"selectedModel")))return m;return NULL;}
static void model_status(void){cJSON *m=selected_model();if(!m){status("Add a GGUF file or model folder, or configure a local server.");EnableWindow(control(ID_SEND),FALSE);return;}
    int ready=0,unavailable=0;cJSON *item;cJSON_ArrayForEach(item,app.models){if(cJSON_IsFalse(get(item,"supported")))unavailable++;else ready++;}
    char info[1024];if(cJSON_IsFalse(get(m,"supported")))snprintf(info,sizeof(info),"Unavailable: %s",str(m,"reason"));else if(!strcmp(str(app.config,"mode"),"ollama"))snprintf(info,sizeof(info),"Selected %s on the local Ollama-compatible server.",str(m,"name"));else snprintf(info,sizeof(info),"%d ready / %d unavailable | %s | %.2f GiB | %s",ready,unavailable,*str(m,"architecture")?str(m,"architecture"):"GGUF",get(m,"size")?get(m,"size")->valuedouble/1073741824.0:0,str(m,"promptSource"));status(info);EnableWindow(control(ID_SEND),!app.worker&&!cJSON_IsFalse(get(m,"supported")));}
static HWND child(HWND owner,const wchar_t *type,const wchar_t *text,DWORD style,int id,int x,int y,int w,int h){
    HWND c=CreateWindowExW(!wcscmp(type,L"EDIT")?WS_EX_CLIENTEDGE:0,type,text,WS_CHILD|WS_VISIBLE|style,scaled(x),scaled(y),scaled(w),scaled(h),owner,(HMENU)(INT_PTR)id,app.instance,NULL);
    SendMessageW(c,WM_SETFONT,(WPARAM)app.font,TRUE);return c;
}
static HWND button(HWND owner,const wchar_t *text,int id,int x,int y,int w,int h){return child(owner,L"BUTTON",text,WS_TABSTOP|BS_OWNERDRAW,id,x,y,w,h);}
static void drawbutton(DRAWITEMSTRUCT *d){
    COLORREF color=(d->CtlID==ID_SEND||d->CtlID==ID_EDITOR_SAVE)?RGB(34,99,123):PANEL;
    if(d->itemState&ODS_SELECTED)color=RGB(47,77,100);HBRUSH brush=CreateSolidBrush(color);FillRect(d->hDC,&d->rcItem,brush);DeleteObject(brush);
    SetBkMode(d->hDC,TRANSPARENT);SetTextColor(d->hDC,(d->itemState&ODS_DISABLED)?RGB(95,111,131):TEXT);SelectObject(d->hDC,app.font);
    wchar_t text[200];GetWindowTextW(d->hwndItem,text,200);RECT r=d->rcItem;InflateRect(&r,-scaled(5),0);DrawTextW(d->hDC,text,-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    if(d->itemState&ODS_FOCUS){InflateRect(&r,-2,-4);DrawFocusRect(d->hDC,&r);}
}
static LRESULT colors(UINT message,WPARAM dc){SetTextColor((HDC)dc,TEXT);SetBkColor((HDC)dc,message==WM_CTLCOLORSTATIC?BACK:PANEL);return (LRESULT)(message==WM_CTLCOLORSTATIC?app.background:app.panel);}
static void append_display(const char *text,int heading){
    HWND h=control(ID_TRANSCRIPT);CHARRANGE end={-1,-1};SendMessageW(h,EM_EXSETSEL,0,(LPARAM)&end);
    CHARFORMAT2W format={0};format.cbSize=sizeof(format);format.dwMask=CFM_COLOR|CFM_BOLD|CFM_FACE|CFM_SIZE;format.crTextColor=heading?CYAN:TEXT;format.dwEffects=heading?CFE_BOLD:0;format.yHeight=220;wcscpy(format.szFaceName,L"Segoe UI");SendMessageW(h,EM_SETCHARFORMAT,SCF_SELECTION,(LPARAM)&format);
    wchar_t *w=wide(text);if(w){SendMessageW(h,EM_REPLACESEL,FALSE,(LPARAM)w);free(w);}SendMessageW(h,EM_SCROLLCARET,0,0);
}
static void render_transcript(void){
    SetWindowTextW(control(ID_TRANSCRIPT),L"");cJSON *m;cJSON *t=thread();
    if(!get(t,"messages")->child){append_display("Welcome to Guilty Spark 343\r\n\r\n",1);append_display("Your local models, conversations and characters.\r\n\r\nChoose a model above, then write a message below. Add a GGUF file to use the bundled engine, or configure a loopback Ollama-compatible server in Settings.\r\n\r\nCtrl+Enter sends a message. Enter inserts a new line.\r\n\r\nConversation history is saved on this Windows account. Import accepts Guilty Spark 343 and supported GobboNet JSON backups and character cards.",0);return;}
    cJSON_ArrayForEach(m,get(t,"messages")){append_display(!strcmp(str(m,"role"),"user")?"You\r\n":"Guilty Spark 343\r\n",1);append_display(str(m,"content"),0);if(*str(m,"content"))append_display("\r\n\r\n",0);}
}
static void render_threads(void){
    HWND list=control(ID_THREADS);SendMessageW(list,LB_RESETCONTENT,0,0);cJSON *t;
    for(int pinned=1;pinned>=0;pinned--)cJSON_ArrayForEach(t,get(app.state,"threads"))if(!strcmp(str(t,"model"),str(app.state,"selectedModel"))&&cJSON_IsTrue(get(t,"pinned"))==pinned){Buffer label={0};if(pinned)adds(&label,"* ");adds(&label,str(t,"name"));wchar_t *w=wide(label.p);LRESULT i=SendMessageW(list,LB_ADDSTRING,0,(LPARAM)w);SendMessageW(list,LB_SETITEMDATA,(WPARAM)i,(LPARAM)t);if(!strcmp(str(t,"id"),str(app.state,"activeThreadId")))SendMessageW(list,LB_SETCURSEL,(WPARAM)i,0);free(w);free(label.p);}
}
static void render_characters(void){
    HWND combo=control(ID_CHARACTERS);SendMessageW(combo,CB_RESETCONTENT,0,0);SendMessageW(combo,CB_ADDSTRING,0,(LPARAM)L"Default assistant");SendMessageW(combo,CB_SETCURSEL,0,0);cJSON *c;int i=1;
    cJSON_ArrayForEach(c,get(app.state,"characters")){wchar_t *w=wide(str(c,"name"));SendMessageW(combo,CB_ADDSTRING,0,(LPARAM)w);if(!strcmp(str(c,"id"),str(app.state,"activeCharacterId")))SendMessageW(combo,CB_SETCURSEL,(WPARAM)i,0);free(w);i++;}
}
static cJSON *sync_aliases(const cJSON *cfg){
    cJSON *map=cJSON_IsObject(get(cfg,"modelAliases"))?cJSON_Duplicate(get(cfg,"modelAliases"),1):cJSON_CreateObject(),*model;
    cJSON_ArrayForEach(model,app.models){const char *name=str(model,"name");setstr(map,name,name);Buffer label={0};adds(&label,name);adds(&label," (Ollama)");if(!get(map,label.p))setstr(map,label.p,name);free(label.p);}return map;
}
static void sync_report(const GaboScan *scan){snprintf(app.syncStatus,sizeof(app.syncStatus),"Gabo net: %d new chats, %d updated, %d messages, %d memories. Pending: %d; preserved conflicts: %d; skipped: %d.",scan->added,scan->updated,scan->messages,scan->memories,scan->pending,scan->conflicts,scan->skipped);}
static void sync_apply(void){
    if(!app.syncReady||app.worker||app.webBusy||!IsWindowEnabled(app.window)||app.closing)return;SyncWork *work=app.syncReady;app.syncReady=NULL;
    app.syncSucceeded=0;if(!work->source)snprintf(app.syncStatus,sizeof(app.syncStatus),"Gabo net sync: %s",work->error);
    else if(remember_draft()){
        cJSON *aliases=sync_aliases(work->config);GaboScan scan;char error[512]={0};
        cJSON *updated=gabo_merge(app.state,work->source,str(work->config,"origin"),aliases,&scan,error);cJSON_Delete(aliases);
        if(updated){
            if(scan.changed&&!save_json(app.statePath,updated,error)){snprintf(app.syncStatus,sizeof(app.syncStatus),"Gabo net sync could not save: %.400s",error);}
            else {if(scan.changed){cJSON_Delete(app.state);app.state=updated;updated=NULL;render_threads();render_transcript();render_memories();if(import_notifications_enabled(app.config)&&(scan.added||scan.memories))queue_import_notifications(app.notices,get(app.state,"gaboActivity")->child);}sync_report(&scan);app.syncSucceeded=1;}
            cJSON_Delete(updated);
        }else snprintf(app.syncStatus,sizeof(app.syncStatus),"Gabo net sync: %.450s",error);
    }else strcpy(app.syncStatus,"Gabo net sync could not apply while the current draft exceeds its limit. Shorten the draft and retry.");
    check_complete();notification_tick();background_report();activity_refresh();status(app.syncStatus);cJSON_Delete(work->config);cJSON_Delete(work->source);free(work);
}
static DWORD WINAPI sync_worker(void *argument){
    SyncWork *work=argument;if(*str(work->config,"file")){wchar_t *file=wide(str(work->config,"file"));work->source=load_json(file,work->error);free(file);}
    else work->source=gabo_browser_snapshot(work->config,app.directory,app.syncCancel,work->error);
    PostMessageW(app.window,WM_SYNC_READY,0,(LPARAM)work);return 0;
}
static void sync_start(int explicitRequest){
    cJSON *cfg=get(app.config,"gaboSync");if(app.closing||app.syncWorker||app.syncReady||!cJSON_IsObject(cfg)||(!explicitRequest&&!cJSON_IsTrue(get(cfg,"enabled"))))return;
    int interval=get(cfg,"intervalSeconds")?get(cfg,"intervalSeconds")->valueint:60;if(interval<15)interval=15;if(interval>3600)interval=3600;app.nextSync=GetTickCount64()+(ULONGLONG)interval*1000;
    SyncWork *work=allocate(sizeof(*work));work->config=cJSON_Duplicate(cfg,1);ResetEvent(app.syncCancel);app.syncWorker=CreateThread(NULL,0,sync_worker,work,0,NULL);
    if(!app.syncWorker){cJSON_Delete(work->config);free(work);strcpy(app.syncStatus,"Cannot start Gabo net sync.");}else strcpy(app.syncStatus,"Checking Gabo net for new conversations...");if(explicitRequest)status(app.syncStatus);
}
static void activity_add_model(const char *model){
    if(!*model)return;cJSON *known=(cJSON*)GetWindowLongPtrW(app.activity,GWLP_USERDATA);if(get(known,model))return;
    setnum(known,model,1);wchar_t *w=wide(model);SendDlgItemMessageW(app.activity,ID_ACTIVITY_MODELS,CB_ADDSTRING,0,(LPARAM)w);free(w);
}
static void activity_refresh(void){
    if(!app.activity)return;cJSON *v,*r;
    cJSON_ArrayForEach(v,app.models)activity_add_model(str(v,"name"));
    cJSON_ArrayForEach(v,get(app.state,"threads"))activity_add_model(str(v,"model"));
    cJSON_ArrayForEach(v,get(app.state,"modelMemories"))activity_add_model(v->string);
    cJSON_ArrayForEach(v,get(app.state,"gaboActivity"))cJSON_ArrayForEach(r,get(v,"models"))activity_add_model(str(r,"model"));
    HWND combo=GetDlgItem(app.activity,ID_ACTIVITY_MODELS),body=GetDlgItem(app.activity,ID_ACTIVITY_TEXT);
    char *model=SendMessageW(combo,CB_GETCURSEL,0,0)>0?input(combo):_strdup("");
    char *text=gabo_activity_text(app.state,app.syncCheck,model,cJSON_IsTrue(get(get(app.config,"gaboSync"),"enabled")),app.syncWorker!=NULL||app.syncReady!=NULL),*old=input(body);
    if(*app.noticeError){Buffer message={0};adds(&message,app.noticeError);adds(&message,"\r\n\r\n");adds(&message,text);free(text);text=message.p;}
    if(strcmp(text,old)){LRESULT scroll=SendMessageW(body,EM_GETFIRSTVISIBLELINE,0,0);DWORD start=0,end=0;SendMessageW(body,EM_GETSEL,(WPARAM)&start,(LPARAM)&end);caption(body,text);SendMessageW(body,EM_SETSEL,start,end);SendMessageW(body,EM_LINESCROLL,0,scroll);}
    free(old);free(text);free(model);EnableWindow(GetDlgItem(app.activity,ID_ACTIVITY_SYNC),!app.syncWorker&&!app.syncReady&&!app.closing);
    SendDlgItemMessageW(app.activity,ID_ACTIVITY_NOTIFY,BM_SETCHECK,import_notifications_enabled(app.config)?BST_CHECKED:BST_UNCHECKED,0);EnableWindow(GetDlgItem(app.activity,ID_ACTIVITY_TEST_NOTICE),import_notifications_enabled(app.config)&&!app.noticeActive&&!cJSON_GetArraySize(app.notices));
}
static LRESULT CALLBACK activity_proc(HWND window,UINT msg,WPARAM w,LPARAM l){
    switch(msg){
    case WM_CREATE:{app.activity=window;SetWindowLongPtrW(window,GWLP_USERDATA,(LONG_PTR)cJSON_CreateObject());
        child(window,L"STATIC",L"Model",SS_LEFT,0,16,18,56,24);
        HWND combo=child(window,L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,ID_ACTIVITY_MODELS,80,14,450,300);SendMessageW(combo,CB_ADDSTRING,0,(LPARAM)L"All models");SendMessageW(combo,CB_SETCURSEL,0,0);
        button(window,L"Check now",ID_ACTIVITY_SYNC,550,14,120,32);
        HWND option=child(window,L"BUTTON",L"Notify about new Gabo net chats and memories",WS_TABSTOP|BS_AUTOCHECKBOX,ID_ACTIVITY_NOTIFY,16,54,510,28);SetWindowTheme(option,L"",L"");button(window,L"Test notification",ID_ACTIVITY_TEST_NOTICE,550,52,160,32);
        HWND body=child(window,L"EDIT",L"",WS_TABSTOP|ES_MULTILINE|ES_READONLY|WS_VSCROLL,ID_ACTIVITY_TEXT,16,96,660,468);SendMessageW(body,EM_LIMITTEXT,4*1024*1024,0);
        activity_refresh();wchar_t *selected=wide(str(app.state,"selectedModel"));LRESULT index=SendMessageW(combo,CB_FINDSTRINGEXACT,(WPARAM)-1,(LPARAM)selected);free(selected);if(index!=CB_ERR)SendMessageW(combo,CB_SETCURSEL,index,0);activity_refresh();SetTimer(window,1,1000,NULL);return 0;}
    case WM_SIZE:{RECT r;GetClientRect(window,&r);MoveWindow(GetDlgItem(window,ID_ACTIVITY_MODELS),scaled(80),scaled(14),r.right-scaled(240),scaled(300),TRUE);MoveWindow(GetDlgItem(window,ID_ACTIVITY_SYNC),r.right-scaled(144),scaled(14),scaled(128),scaled(32),TRUE);MoveWindow(GetDlgItem(window,ID_ACTIVITY_NOTIFY),scaled(16),scaled(54),r.right-scaled(200),scaled(28),TRUE);MoveWindow(GetDlgItem(window,ID_ACTIVITY_TEST_NOTICE),r.right-scaled(184),scaled(52),scaled(168),scaled(32),TRUE);MoveWindow(GetDlgItem(window,ID_ACTIVITY_TEXT),scaled(16),scaled(96),r.right-scaled(32),r.bottom-scaled(112),TRUE);return 0;}
    case WM_GETMINMAXINFO:((MINMAXINFO*)l)->ptMinTrackSize.x=scaled(690);((MINMAXINFO*)l)->ptMinTrackSize.y=scaled(480);return 0;
    case WM_COMMAND:if(LOWORD(w)==ID_ACTIVITY_MODELS&&HIWORD(w)==CBN_SELCHANGE){SendDlgItemMessageW(window,ID_ACTIVITY_TEXT,EM_SETSEL,0,0);SendDlgItemMessageW(window,ID_ACTIVITY_TEXT,EM_SCROLLCARET,0,0);activity_refresh();}else if(LOWORD(w)==ID_ACTIVITY_SYNC){sync_start(1);activity_refresh();}else if(LOWORD(w)==ID_ACTIVITY_NOTIFY)notification_toggle();else if(LOWORD(w)==ID_ACTIVITY_TEST_NOTICE&&import_notifications_enabled(app.config)&&!app.noticeActive&&!cJSON_GetArraySize(app.notices)){cJSON *test=cJSON_CreateObject();cJSON_AddBoolToObject(test,"test",1);cJSON_AddItemToArray(app.notices,test);notification_tick();activity_refresh();}return 0;
    case WM_TIMER:activity_refresh();return 0;
    case WM_DRAWITEM:drawbutton((DRAWITEMSTRUCT*)l);return TRUE;
    case WM_CTLCOLORBTN:return colors(WM_CTLCOLORSTATIC,w);
    case WM_CTLCOLORSTATIC:case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX:return colors(msg,w);
    case WM_CLOSE:DestroyWindow(window);return 0;
    case WM_DESTROY:cJSON_Delete((cJSON*)GetWindowLongPtrW(window,GWLP_USERDATA));app.activity=NULL;return 0;
    }return DefWindowProcW(window,msg,w,l);
}
static void activity_open(void){
    if(!IsWindowEnabled(app.window))return;show_main();if(app.activity){activity_refresh();ShowWindow(app.activity,SW_RESTORE);SetForegroundWindow(app.activity);return;}
    RECT parent;GetWindowRect(app.window,&parent);HWND window=CreateWindowExW(0,L"SparkActivity",L"Gabo net import activity",WS_OVERLAPPEDWINDOW,parent.left+scaled(40),parent.top+scaled(30),scaled(860),scaled(680),app.window,NULL,app.instance,NULL);BOOL dark=TRUE;DwmSetWindowAttribute(window,20,&dark,sizeof(dark));ShowWindow(window,SW_SHOW);SetForegroundWindow(window);
}
static void notification_cancel(void){
    if(app.noticeActive&&app.trayAdded){NOTIFYICONDATAW icon={sizeof(icon)};icon.hWnd=app.window;icon.uID=343;icon.uFlags=NIF_INFO;Shell_NotifyIconW(NIM_MODIFY,&icon);}
    cJSON_Delete(app.noticeActive);app.noticeActive=NULL;app.noticeWasShown=0;app.noticeRetry=GetTickCount64()+1000;
}
static void notification_toggle(void){
    cJSON *candidate=cJSON_Duplicate(app.config,1),*cfg=get(candidate,"gaboSync");if(!cJSON_IsObject(cfg)){cJSON_DeleteItemFromObjectCaseSensitive(candidate,"gaboSync");cfg=cJSON_AddObjectToObject(candidate,"gaboSync");}
    int enabled=!import_notifications_enabled(app.config);cJSON_DeleteItemFromObjectCaseSensitive(cfg,"notifications");cJSON_AddBoolToObject(cfg,"notifications",enabled);char error[512]={0};
    if(!save_json(app.configPath,candidate,error)){cJSON_Delete(candidate);errorbox(app.activity?app.activity:app.window,error);activity_refresh();return;}
    cJSON_Delete(app.config);app.config=candidate;if(!enabled){notification_cancel();cJSON_Delete(app.notices);app.notices=cJSON_CreateArray();app.noticeError[0]=0;}
    background_report();activity_refresh();
}
static void notification_tick(void){
    if(app.closing)return;
    if(app.noticeOpen&&IsWindowEnabled(app.window)){activity_open();if(app.activity){SendDlgItemMessageW(app.activity,ID_ACTIVITY_MODELS,CB_SETCURSEL,0,0);activity_refresh();app.noticeOpen=0;}}
    if(!import_notifications_enabled(app.config))return;
    // Normally Windows ends a notification with HIDE, TIMEOUT or USERCLICK.
    // Do not let an unconfirmed, suppressed banner block the queue forever.
    if(app.noticeActive){if(!app.noticeWasShown&&GetTickCount64()-app.noticeStarted>120000){notification_cancel();setstr(app.noticeLast,"status","display unconfirmed");background_report();}return;}
    if(!app.notices||!app.notices->child||!app.trayAdded||GetTickCount64()<app.noticeRetry)return;
    app.noticeActive=cJSON_DetachItemFromArray(app.notices,0);app.noticeWasShown=0;app.noticeStarted=GetTickCount64();cJSON_Delete(app.noticeLast);app.noticeLast=cJSON_Duplicate(app.noticeActive,1);char now[32];spark_utc(now);setstr(app.noticeLast,"submittedAtUtc",now);setstr(app.noticeLast,"status","submitting");
    NOTIFYICONDATAW icon={sizeof(icon)};icon.hWnd=app.window;icon.uID=343;icon.uFlags=NIF_INFO;icon.dwInfoFlags=NIIF_INFO|NIIF_RESPECT_QUIET_TIME;import_notification_text(app.noticeActive,icon.szInfoTitle,icon.szInfo);
    if(Shell_NotifyIconW(NIM_MODIFY,&icon)){app.noticeSubmitted++;app.noticeError[0]=0;if(!strcmp(str(app.noticeLast,"status"),"submitting"))setstr(app.noticeLast,"status","submitted");}
    else {if(app.noticeActive){cJSON_InsertItemInArray(app.notices,0,app.noticeActive);app.noticeActive=NULL;}app.noticeRetry=GetTickCount64()+30000;strcpy(app.noticeError,"Windows could not accept the notification. 343 will retry; import details are saved in Import activity.");setstr(app.noticeLast,"status","not submitted");}
    background_report();
}
static void notification_callback(UINT event){
    // Clicking any retained Windows banner opens all model groups. A tray
    // callback has no per-banner identity, so never guess which older banner
    // the user clicked and accidentally select a different model's activity.
    if(event==NIN_BALLOONUSERCLICK){app.noticeOpen=1;show_main();}
    if(app.noticeActive){
        if(event==NIN_BALLOONSHOW){if(!app.noticeWasShown)app.noticeShown++;app.noticeWasShown=1;setstr(app.noticeLast,"status","shown");}
        else if(event==NIN_BALLOONHIDE||event==NIN_BALLOONTIMEOUT||event==NIN_BALLOONUSERCLICK){setstr(app.noticeLast,"status",event==NIN_BALLOONUSERCLICK?"clicked":app.noticeWasShown?"dismissed":"suppressed");cJSON_Delete(app.noticeActive);app.noticeActive=NULL;app.noticeRetry=GetTickCount64()+1000;}
    }background_report();if(app.noticeOpen)notification_tick();
}
static void character_status(void){cJSON *card=find_id(get(app.state,"characters"),str(app.state,"activeCharacterId"));char info[1024];
    if(*str(app.state,"selectedModel"))snprintf(info,sizeof(info),"%s is saved for %s. It will return when you select this model.",card?str(card,"name"):"Default assistant",str(app.state,"selectedModel"));
    else snprintf(info,sizeof(info),"%s will be assigned to the first model you select.",card?str(card,"name"):"Default assistant");status(info);
}
static int activate_model(const char *name){if(!remember_draft())return 0;char error[512]={0};int ok=select_model(app.state,name,error);render_characters();render_memories();if(ok){render_threads();render_transcript();render_draft();}else status(error);return ok;}
static void render_models(void){
    HWND combo=control(ID_MODELS);SendMessageW(combo,CB_RESETCONTENT,0,0);cJSON *m;int selected=-1,i=0;
    cJSON_ArrayForEach(m,app.models){Buffer label={0};adds(&label,str(m,"name"));if(cJSON_IsFalse(get(m,"supported")))adds(&label," [unavailable]");wchar_t *w=wide(label.p);SendMessageW(combo,CB_ADDSTRING,0,(LPARAM)w);if(!strcmp(str(m,"name"),str(app.state,"selectedModel")))selected=i;free(w);free(label.p);i++;}
    if(selected<0&&i){selected=0;for(cJSON *v=app.models->child;v;v=v->next){if(!cJSON_IsFalse(get(v,"supported")))break;selected++;}if(selected>=i)selected=0;}
    if(!i){SendMessageW(combo,CB_ADDSTRING,0,(LPARAM)L"Add a GGUF or configure a server");selected=0;}
    if(i&&!activate_model(str(cJSON_GetArrayItem(app.models,selected),"name"))){selected=-1;int index=0;cJSON_ArrayForEach(m,app.models){if(!strcmp(str(m,"name"),str(app.state,"selectedModel")))selected=index;index++;}}
    SendMessageW(combo,CB_SETCURSEL,(WPARAM)selected,0);
    SendMessageW(combo,CB_SETDROPPEDWIDTH,scaled(680),0);
}
static void busy(int enabled){
    int ids[]={ID_THREADS,ID_NEW,ID_RENAME,ID_DELETE,ID_PIN,ID_IMPORT,ID_EXPORT,ID_MODELS,ID_REFRESH,ID_ADD_MODEL,ID_ADD_FOLDER,ID_MODEL_INFO,ID_CHARACTERS,ID_EDIT_CHARACTER,ID_SETTINGS,ID_SEND};
    for(unsigned i=0;i<sizeof(ids)/sizeof(ids[0]);i++)EnableWindow(control(ids[i]),!enabled);EnableWindow(control(ID_MEMORIES),!enabled&&*str(app.state,"selectedModel"));EnableWindow(control(ID_STOP),enabled);InvalidateRect(app.window,NULL,FALSE);
}
static int delta(const char *text,void *context){(void)context;char *copy=_strdup(text);if(!copy)return 0;if(!PostMessageW(app.window,WM_DELTA,0,(LPARAM)copy)){free(copy);return 0;}return 1;}
static DWORD WINAPI worker(void *argument){Work *work=argument;
    if(work->kind==0){work->models=list_models(work->config,app.cancel,work->error);work->ok=work->models!=NULL;}
    else work->ok=generate(&app.runner,work->config,work->state,work->thread,app.cancel,delta,NULL,work->error);
    PostMessageW(app.window,WM_FINISHED,0,(LPARAM)work);return 0;
}
static void launch_work(Work *work){
    ResetEvent(app.cancel);busy(1);app.worker=CreateThread(NULL,0,worker,work,0,NULL);
    if(!app.worker){strcpy(work->error,"Cannot create the inference worker.");work->ok=0;PostMessageW(app.window,WM_FINISHED,0,(LPARAM)work);}
}
static void refresh(void){if(app.worker)return;Work *work=allocate(sizeof(*work));work->config=cJSON_Duplicate(app.config,1);status("Inspecting available models...");launch_work(work);}
static void send_message(void){
    if(app.worker||app.webBusy||!*str(app.state,"selectedModel"))return;if(cJSON_IsFalse(get(selected_model(),"supported"))){model_status();return;}char *text=input(control(ID_PROMPT));if(!*text){free(text);return;}
    if(strlen(text)>SPARK_TEXT){free(text);status("Message exceeds the text limit.");return;}
    cJSON *t=thread(),*messages=get(t,"messages");if(cJSON_GetArraySize(messages)>998||message_total()>4998){free(text);status("Message limit reached. Export a backup and remove older conversations.");return;}
    char *snapshot=cJSON_PrintUnformatted(app.state);size_t bytes=strlen(snapshot);free(snapshot);if(bytes+strlen(text)>SPARK_LIMIT-400000){free(text);status("Export a backup and remove older conversations before adding more history.");return;}
    char memoryError[512]={0};int memoryChanged=remember_message(app.state,text,memoryError);render_memories();
    cJSON *user=cJSON_CreateObject();char id[64];newid(id);setstr(user,"id",id);setstr(user,"role","user");setstr(user,"content",text);cJSON_AddItemToArray(messages,user);
    if(!strcmp(str(t,"name"),"New conversation")){wchar_t *w=wide(text);if(w){if(wcslen(w)>60)w[60]=0;char *name=utf8(w);if(name){setstr(t,"name",name);free(name);}free(w);}}
    Work *work=allocate(sizeof(*work));work->kind=1;work->config=cJSON_Duplicate(app.config,1);work->state=cJSON_Duplicate(app.state,1);work->thread=cJSON_Duplicate(t,1);
    work->memoryChanged=memoryChanged>0;snprintf(work->memoryError,sizeof(work->memoryError),"%s",memoryError);
    cJSON_ReplaceItemInObjectCaseSensitive(work->config,"models",cJSON_Duplicate(app.models,1));
    app.reply=cJSON_CreateObject();newid(id);setstr(app.reply,"id",id);setstr(app.reply,"role","assistant");setstr(app.reply,"content","");cJSON_AddItemToArray(messages,app.reply);app.replyThread=t;
    setstr(t,"draft","");SetWindowTextW(control(ID_PROMPT),L"");render_threads();render_transcript();status(memoryChanged>0?"Memories updated for this model. Generating locally...":"Loading model / generating locally... Stop cancels this request.");if(!save())snprintf(work->memoryError,sizeof(work->memoryError),"Workspace could not be saved to disk; retry saving before closing.");free(text);launch_work(work);
}
static int choose_file(HWND owner,wchar_t *path,int saving,int model){
    OPENFILENAMEW o={sizeof(o)};o.hwndOwner=owner;o.lpstrFile=path;o.nMaxFile=32768;
    o.lpstrFilter=model?L"GGUF model (*.gguf)\0*.gguf\0All files (including Ollama blobs)\0*.*\0":L"JSON backup or character (*.json)\0*.json\0\0";
    o.Flags=OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|(saving?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST);o.lpstrDefExt=model?NULL:L"json";return saving?GetSaveFileNameW(&o):GetOpenFileNameW(&o);
}
static void import_file(void){
    if(!remember_draft())return;save();
    wchar_t *path=allocate(32768*sizeof(wchar_t));if(choose_file(app.window,path,0,0)){char err[512]={0};cJSON *raw=load_json(path,err);if(raw){int backup=get(raw,"threads")||get(raw,"characters")||get(raw,"characterCards");char *json=cJSON_PrintUnformatted(raw);cJSON *state=import_state(json,app.state,err);free(json);cJSON_Delete(raw);
        if(state){if(!backup||MessageBoxW(app.window,L"Replace the workspace with this backup? The previous saved workspace will be retained as workspace.json.bak.",L"Import workspace",MB_YESNO|MB_ICONQUESTION)==IDYES){cJSON_Delete(app.state);app.state=state;render_threads();render_characters();render_draft();render_models();render_transcript();save();status("Imported supported chat/character/persona fields. Scripts, credentials, variants, sampler settings and automation were omitted.");}else cJSON_Delete(state);}else errorbox(app.window,err);
    }else errorbox(app.window,err);}free(path);
}
static void export_file(void){if(!remember_draft())return;wchar_t *path=allocate(32768*sizeof(wchar_t));wcscpy(path,L"guilty-spark-343.json");if(choose_file(app.window,path,1,0)){char err[512]={0};if(!save_json(path,app.state,err))errorbox(app.window,err);else status("Workspace exported as portable JSON.");}free(path);}
static void add_model(void){
    if(strcmp(str(app.config,"mode"),"local")){status("Switch to the bundled GGUF engine in Settings before adding a file.");return;}
    if(cJSON_GetArraySize(get(app.config,"models"))>=128){status("Local model limit reached.");return;}
    wchar_t *path=allocate(32768*sizeof(wchar_t));if(choose_file(app.window,path,0,1)){wchar_t *name=wcsrchr(path,L'\\');name=name?name+1:path;char *p=utf8(path),*n=utf8(name);cJSON *m=cJSON_CreateObject();char id[64];newid(id);Buffer label={0};adds(&label,n?n:"Local model");adds(&label," [");append(&label,id,8);adds(&label,"]");setstr(m,"name",label.p);setstr(m,"path",p);cJSON_AddItemToArray(get(app.config,"models"),m);activate_model(label.p);free(p);free(n);free(label.p);save_config();save();refresh();}free(path);
}
static void folder_add(cJSON *config,const wchar_t *path){char *text=utf8(path);if(!text)return;cJSON *folders=get(config,"modelFolders"),*f;cJSON_ArrayForEach(f,folders)if(cJSON_IsString(f)&&!_stricmp(f->valuestring,text)){free(text);return;}if(cJSON_GetArraySize(folders)<32)cJSON_AddItemToArray(folders,cJSON_CreateString(text));free(text);}
static void add_folder(void){
    if(strcmp(str(app.config,"mode"),"local")){status("Select the bundled GGUF engine in Settings to scan model folders.");return;}
    if(cJSON_GetArraySize(get(app.config,"modelFolders"))>=32){status("Model folder limit reached (32).");return;}
    BROWSEINFOW browse={0};browse.hwndOwner=app.window;browse.lpszTitle=L"Choose a folder of GGUF files, or an Ollama models folder containing manifests and blobs.";browse.ulFlags=BIF_RETURNONLYFSDIRS|BIF_NEWDIALOGSTYLE|BIF_EDITBOX;
    PIDLIST_ABSOLUTE choice=SHBrowseForFolderW(&browse);if(choice){wchar_t path[32768];if(SHGetPathFromIDListEx(choice,path,32768,0)){folder_add(app.config,path);save_config();refresh();}CoTaskMemFree(choice);}
}
static void model_info(void){cJSON *m=selected_model();if(!m)return;Buffer text={0};adds(&text,str(m,"name"));adds(&text,"\n\n");adds(&text,str(m,"reason"));adds(&text,"\n\nPrompt: ");adds(&text,str(m,"promptSource"));adds(&text,"\nArchitecture: ");adds(&text,str(m,"architecture"));adds(&text,"\nSource: ");adds(&text,str(m,"source"));adds(&text,"\nFile: ");adds(&text,str(m,"path"));adds(&text,"\n\nReady to load means the file can be submitted to the runtime; it is not a guarantee of architecture support or enough RAM. Choose a different local runtime in Settings if needed.");wchar_t *w=wide(text.p);MessageBoxW(app.window,w,L"Model details",MB_OK|MB_ICONINFORMATION);free(w);free(text.p);}
static void new_conversation(void){if(!remember_draft())return;char error[512]={0};if(!create_conversation(app.state,error)){status(error);return;}
    render_threads();render_transcript();render_draft();save();SetFocus(control(ID_PROMPT));
}
static void editor_open(int kind);
static cJSON *web_error(WebCall *call,int code,const char *text){call->status=code;cJSON *out=cJSON_CreateObject();setstr(out,"error",text);return out;}
static cJSON *web_with_revision(const char *model){cJSON *out=web_profile(app.state,model);char *json=cJSON_PrintUnformatted(out),hash[65];spark_digest(json,"",hash);free(json);setstr(out,"sparkRevision",hash);return out;}
static cJSON *web_generation(WebCall *call){
    if(app.worker||app.webBusy||!IsWindowEnabled(app.window))return web_error(call,409,"343 is busy. Finish the current action before generating.");
    cJSON *items=get(call->body,"messages");if(!cJSON_IsArray(items)||!items->child||cJSON_GetArraySize(items)>1000)return web_error(call,400,"A generation needs a bounded message array.");
    cJSON *owner=find_id(get(app.state,"threads"),str(call->body,"sparkThreadId"));if(owner&&strcmp(str(owner,"model"),call->model))return web_error(call,400,"This conversation belongs to another model.");
    cJSON *state=new_state(),*t=get(state,"threads")->child,*config=cJSON_Duplicate(app.config,1);setstr(state,"selectedModel",call->model);setstr(t,"model",call->model);cJSON_ReplaceItemInObjectCaseSensitive(state,"modelMemories",cJSON_Duplicate(get(app.state,"modelMemories"),1));
    cJSON_ReplaceItemInObjectCaseSensitive(config,"models",cJSON_Duplicate(app.models,1));cJSON *settings=get(state,"settings"),*temperature=get(call->body,"temperature"),*maximum=get(call->body,"max_tokens");
    if(temperature&&(!cJSON_IsNumber(temperature)||temperature->valuedouble<0||temperature->valuedouble>2)){cJSON_Delete(state);cJSON_Delete(config);return web_error(call,400,"Temperature must be between 0 and 2.");}
    if(maximum&&(!cJSON_IsNumber(maximum)||maximum->valuedouble < -1||maximum->valuedouble>32768)){cJSON_Delete(state);cJSON_Delete(config);return web_error(call,400,"Output token limit must be 1 to 32768, or -1 for the desktop default.");}
    setnum(settings,"temperature",temperature?temperature->valuedouble:get(get(app.state,"settings"),"temperature")->valuedouble);setnum(settings,"numPredict",maximum&&maximum->valueint>0?maximum->valueint:get(get(app.state,"settings"),"numPredict")->valueint);
    char parameterError[512];cJSON *parameters=generation_parameters(call->body,!strcmp(str(config,"mode"),"ollama"),parameterError);if(!parameters){cJSON_Delete(state);cJSON_Delete(config);return web_error(call,400,parameterError);}cJSON_AddItemToObject(state,"generationParameters",parameters);
    Buffer system={0};cJSON *m;cJSON_ArrayForEach(m,items){const char *role=str(m,"role");if(!cJSON_IsString(get(m,"content"))||strlen(str(m,"content"))>SPARK_TEXT||get(m,"tool_calls")||(strcmp(role,"system")&&strcmp(role,"user")&&strcmp(role,"assistant"))){free(system.p);cJSON_Delete(state);cJSON_Delete(config);return web_error(call,400,"Only bounded system, user and assistant text is supported.");}
        if(!strcmp(role,"system")){if(system.n)adds(&system,"\n\n");adds(&system,str(m,"content"));}else{cJSON *copy=cJSON_CreateObject();setstr(copy,"role",role);setstr(copy,"content",str(m,"content"));cJSON_AddItemToArray(get(t,"messages"),copy);}}
    if(system.n>SPARK_TEXT){free(system.p);cJSON_Delete(state);cJSON_Delete(config);return web_error(call,400,"System instructions exceed the text limit.");}setstr(state,"persona",system.p?system.p:"");free(system.p);
    cJSON *out=cJSON_CreateObject();cJSON_AddItemToObject(out,"state",state);cJSON_AddItemToObject(out,"thread",cJSON_Duplicate(t,1));cJSON_AddItemToObject(out,"config",config);app.webBusy=1;busy(1);EnableWindow(control(ID_STOP),FALSE);status("Generating in the advanced workspace. Its Stop button cancels the request.");return out;
}
static cJSON *web_action(WebCall *call){
    if(app.closing)return web_error(call,503,"343 is closing.");
    if(!strcmp(call->route,"/generation-finished")){app.webBusy=0;if(!app.worker)busy(0);status("Advanced workspace generation finished.");return cJSON_CreateObject();}
    if(!strcmp(call->route,"/models")){cJSON *out=cJSON_CreateObject(),*list=cJSON_AddArrayToObject(out,"models"),*m;cJSON_ArrayForEach(m,app.models){cJSON *item=cJSON_CreateObject();setstr(item,"name",str(m,"name"));cJSON_AddBoolToObject(item,"available",!cJSON_IsFalse(get(m,"supported")));cJSON_AddItemToArray(list,item);}setstr(out,"selectedModel",str(app.state,"selectedModel"));return out;}
    cJSON *model=NULL,*m;cJSON_ArrayForEach(m,app.models)if(!strcmp(str(m,"name"),call->model))model=m;
    if(!model)return web_error(call,404,"This model is not in the 343 catalog.");
    if(!strcmp(call->route,"/runtime-config")){if(app.worker||app.webBusy)return web_error(call,409,"343 is busy. Retry model tools after generation.");if(!strcmp(str(app.config,"mode"),"ollama"))return web_error(call,400,"Tokenization and template tools require a local GGUF runtime.");cJSON *out=cJSON_Duplicate(app.config,1);cJSON_AddItemToObject(out,"sparkToolModel",cJSON_Duplicate(model,1));app.webBusy=1;busy(1);EnableWindow(control(ID_STOP),FALSE);return out;}
    if(!strcmp(call->route,"/state")){
        if(!strcmp(call->method,"GET"))return web_with_revision(call->model);
        if(strcmp(call->method,"PUT"))return web_error(call,405,"Use GET or PUT for a model workspace.");
        if(app.worker||!IsWindowEnabled(app.window))return web_error(call,409,"343 is busy. The browser will retry saving.");
        if(!remember_draft())return web_error(call,409,"Finish editing the desktop draft before synchronizing.");
        cJSON *current=web_with_revision(call->model);int fresh=!strcmp(str(current,"sparkRevision"),str(call->body,"sparkRevision"));cJSON_Delete(current);if(!fresh)return web_error(call,409,"This model changed in another window. Reload after exporting your browser copy.");
        char error[512]={0};cJSON *next=web_save_profile(app.state,call->body,call->model,error);if(!next)return web_error(call,400,error);if(!save_json(app.statePath,next,error)){cJSON_Delete(next);return web_error(call,500,error);}cJSON_Delete(app.state);app.state=next;render_threads();render_characters();render_memories();render_transcript();render_draft();return web_with_revision(call->model);
    }
    if(!strcmp(call->route,"/state/info")){cJSON *out=cJSON_CreateObject();cJSON_AddBoolToObject(out,"exists",1);setnum(out,"mtime",0);return out;}
    if(!strcmp(call->route,"/active-model.json")){cJSON *out=cJSON_CreateObject();setstr(out,"id",call->model);setstr(out,"name",call->model);setstr(out,"ggufFile",call->model);setstr(out,"family",str(model,"architecture"));setstr(out,"thinkingFormat","none");setnum(out,"maxCtx",get(app.config,"contextSize")->valuedouble);setnum(out,"defaultCtx",get(app.config,"contextSize")->valuedouble);return out;}
    if(!strcmp(call->route,"/models-list.json")){cJSON *out=cJSON_CreateObject(),*models=cJSON_AddArrayToObject(out,"models");setstr(out,"activeFile",call->model);cJSON_ArrayForEach(m,app.models)if(!cJSON_IsFalse(get(m,"supported"))){cJSON *item=cJSON_CreateObject();setstr(item,"name",str(m,"name"));setstr(item,"file",str(m,"name"));cJSON_AddItemToArray(models,item);}return out;}
    if(!strcmp(call->route,"/llm/health")){if(cJSON_IsFalse(get(model,"supported")))return web_error(call,503,str(model,"reason"));return cJSON_Parse("{\"status\":\"ok\"}");}
    if(!strcmp(call->route,"/generation")){if(cJSON_IsFalse(get(model,"supported")))return web_error(call,400,str(model,"reason"));return web_generation(call);}
    if(!strcmp(call->route,"/embedding-config"))return cJSON_Duplicate(app.config,1);
    if(!strcmp(call->route,"/perf")){
        if(!strcmp(call->method,"POST")){
            if(app.worker||app.webBusy)return web_error(call,409,"Finish generation before changing runtime settings.");
            cJSON *ctx=get(call->body,"ctxSize"),*gpu=get(call->body,"gpuLayers");const char *kv=str(call->body,"kvCacheType");int reset=cJSON_IsTrue(get(call->body,"reset"));
            if(!reset&&(!cJSON_IsNumber(ctx)||ctx->valueint<128||ctx->valueint>131072||!cJSON_IsNumber(gpu)||gpu->valueint<0||gpu->valueint>1000||(strcmp(kv,"f16")&&strcmp(kv,"q8_0")&&strcmp(kv,"q4_0"))))return web_error(call,400,"Check context size (128-131072), GPU layers (0-1000), and KV type (f16, q8_0 or q4_0).");
            cJSON *updated=cJSON_Duplicate(app.config,1);setnum(updated,"contextSize",reset?2048:ctx->valueint);setnum(updated,"gpuLayers",reset?0:gpu->valueint);setstr(updated,"kvCacheType",reset?"f16":kv);char error[512]={0};if(!save_json(app.configPath,updated,error)){cJSON_Delete(updated);return web_error(call,500,error);}cJSON_Delete(app.config);app.config=updated;runner_stop(&app.runner);app.webBusy=1;busy(1);
        }else if(strcmp(call->method,"GET"))return web_error(call,405,"Use GET or POST for performance settings.");
        cJSON *out=cJSON_CreateObject(),*current=cJSON_AddObjectToObject(out,"current");setnum(current,"ctxSize",get(app.config,"contextSize")->valueint);setnum(current,"gpuLayers",get(app.config,"gpuLayers")->valueint);setstr(current,"kvCacheType",*str(app.config,"kvCacheType")?str(app.config,"kvCacheType"):"f16");cJSON_AddItemToObject(out,"auto",cJSON_Parse("{\"ctxSize\":2048,\"gpuLayers\":0,\"kvCacheType\":\"f16\"}"));setnum(out,"modelMaxCtx",131072);cJSON_AddBoolToObject(out,"overridden",1);return out;
    }
    if(!strcmp(call->route,"/spark/memories")){
        cJSON *copy=cJSON_Duplicate(app.state,1);setstr(copy,"selectedModel",call->model);
        if(!strcmp(call->method,"PUT")){if(app.worker||app.webBusy){cJSON_Delete(copy);return web_error(call,409,"Finish the current generation first.");}char error[512]={0};int ok=set_memories(copy,str(call->body,"text"),cJSON_IsTrue(get(call->body,"enabled")),cJSON_IsTrue(get(call->body,"automatic")),error);setstr(copy,"selectedModel",str(app.state,"selectedModel"));if(ok)ok=save_json(app.statePath,copy,error);if(!ok){cJSON_Delete(copy);return web_error(call,400,error);}cJSON_Delete(app.state);app.state=copy;copy=cJSON_Duplicate(app.state,1);setstr(copy,"selectedModel",call->model);render_memories();}
        cJSON *out=cJSON_CreateObject();char *notes=memory_text(copy);setstr(out,"text",notes);free(notes);cJSON_AddBoolToObject(out,"enabled",memory_option(copy,"enabled"));cJSON_AddBoolToObject(out,"automatic",memory_option(copy,"automatic"));cJSON_Delete(copy);return out;
    }
    return web_error(call,404,"This advanced-workspace endpoint is not yet implemented.");
}
static LRESULT CALLBACK input_proc(HWND window,UINT msg,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR ref){(void)id;(void)ref;if(msg==WM_KEYDOWN&&w==VK_RETURN&&(GetKeyState(VK_CONTROL)&0x8000)){PostMessageW(app.window,WM_COMMAND,ID_SEND,0);return 0;}return DefSubclassProc(window,msg,w,l);}
static void layout(void){RECT r;GetClientRect(app.window,&r);int w=r.right,h=r.bottom,left=scaled(242),gap=scaled(16),right=w-left-gap;
    MoveWindow(control(ID_THREADS),gap,scaled(125),left-gap*2,h-scaled(479),TRUE);
    MoveWindow(control(ID_ACTIVITY),gap,h-scaled(334),scaled(209),scaled(34),TRUE);
    MoveWindow(control(ID_PHONE),gap,h-scaled(290),scaled(209),scaled(34),TRUE);
    MoveWindow(control(ID_WEB),gap,h-scaled(246),scaled(209),scaled(34),TRUE);
    MoveWindow(control(ID_SYNC),gap,h-scaled(202),scaled(209),scaled(34),TRUE);
    int ids[]={ID_NEW,ID_RENAME,ID_DELETE,ID_PIN};for(int i=0;i<4;i++)MoveWindow(control(ids[i]),gap+(i%2)*scaled(108),h-scaled(158)+(i/2)*scaled(42),scaled(101),scaled(34),TRUE);
    MoveWindow(control(ID_IMPORT),gap,h-scaled(68),scaled(101),scaled(34),TRUE);MoveWindow(control(ID_EXPORT),gap+scaled(108),h-scaled(68),scaled(101),scaled(34),TRUE);
    MoveWindow(control(ID_MODELS),left,scaled(82),right/2-scaled(92),scaled(300),TRUE);MoveWindow(control(ID_REFRESH),left+right/2-scaled(86),scaled(82),scaled(78),scaled(30),TRUE);
    MoveWindow(control(ID_CHARACTERS),left+right/2,scaled(82),right/2-scaled(92),scaled(300),TRUE);MoveWindow(control(ID_EDIT_CHARACTER),w-gap-scaled(84),scaled(82),scaled(84),scaled(30),TRUE);
    MoveWindow(control(ID_SETTINGS),w-gap-scaled(94),scaled(20),scaled(94),scaled(32),TRUE);MoveWindow(control(ID_ADD_MODEL),w-gap-scaled(204),scaled(20),scaled(102),scaled(32),TRUE);
    MoveWindow(control(ID_ADD_FOLDER),w-gap-scaled(314),scaled(20),scaled(102),scaled(32),TRUE);MoveWindow(control(ID_MODEL_INFO),w-gap-scaled(424),scaled(20),scaled(102),scaled(32),TRUE);
    MoveWindow(control(ID_MEMORIES),w-gap-scaled(540),scaled(20),scaled(108),scaled(32),TRUE);
    MoveWindow(control(ID_TRANSCRIPT),left,scaled(128),right,h-scaled(320),TRUE);MoveWindow(control(ID_PROMPT),left,h-scaled(172),right,scaled(92),TRUE);
    MoveWindow(control(ID_STOP),w-gap-scaled(190),h-scaled(66),scaled(88),scaled(34),TRUE);MoveWindow(control(ID_SEND),w-gap-scaled(94),h-scaled(66),scaled(94),scaled(34),TRUE);
    MoveWindow(control(ID_STATUS),left,h-scaled(28),right,scaled(22),TRUE);InvalidateRect(app.window,NULL,TRUE);
}
static void capture(const wchar_t *file){
    RECT r;GetWindowRect(app.window,&r);int width=r.right-r.left,height=r.bottom-r.top;HDC screen=GetWindowDC(app.window),dc=CreateCompatibleDC(screen);HBITMAP bmp=CreateCompatibleBitmap(screen,width,height);HGDIOBJ old=SelectObject(dc,bmp);PrintWindow(app.window,dc,0);
    BITMAPINFO info={0};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    DWORD bytes=(DWORD)width*(DWORD)height*4;void *pixels=allocate(bytes);SelectObject(dc,old);GetDIBits(dc,bmp,0,(UINT)height,pixels,&info,DIB_RGB_COLORS);
    BITMAPFILEHEADER header={0};header.bfType=0x4d42;header.bfOffBits=sizeof(header)+sizeof(BITMAPINFOHEADER);header.bfSize=header.bfOffBits+bytes;
    HANDLE out=CreateFileW(file,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,0,NULL);DWORD count;if(out!=INVALID_HANDLE_VALUE){WriteFile(out,&header,sizeof(header),&count,NULL);WriteFile(out,&info.bmiHeader,sizeof(info.bmiHeader),&count,NULL);WriteFile(out,pixels,bytes,&count,NULL);CloseHandle(out);}free(pixels);DeleteObject(bmp);DeleteDC(dc);ReleaseDC(app.window,screen);
}
static void smoke_result(Work *work){
    cJSON *receipt=cJSON_CreateObject();int ok=app.smokeAction?(!work->ok&&app.reply&&*str(app.reply,"content")&&WaitForSingleObject(app.cancel,0)==WAIT_OBJECT_0&&!app.runner.process):(work->ok&&app.reply&&*str(app.reply,"content"));cJSON_AddBoolToObject(receipt,"passed",ok);setstr(receipt,"application","Guilty Spark 343 native C / Win32");setstr(receipt,"scenario",app.smokeAction==1?"stop":app.smokeAction==2?"close":"generation");cJSON_AddBoolToObject(receipt,"owned_runner_alive",app.runner.process&&WaitForSingleObject(app.runner.process,0)==WAIT_TIMEOUT);setstr(receipt,"model",str(app.state,"selectedModel"));setstr(receipt,"response",str(app.reply,"content"));setstr(receipt,"error",work->error);
    cJSON_AddBoolToObject(receipt,"workspace_saved",GetFileAttributesW(app.statePath)!=INVALID_FILE_ATTRIBUTES);char err[512]={0};if(!save_json(app.receiptPath,receipt,err))ok=0;cJSON_Delete(receipt);app.exitCode=!ok;
    wchar_t *image=allocate(32768*sizeof(wchar_t));swprintf(image,32768,L"%ls.bmp",app.receiptPath);UpdateWindow(app.window);capture(image);free(image);SetTimer(app.window,2,500,NULL);
}
static LRESULT CALLBACK window_proc(HWND window,UINT msg,WPARAM w,LPARAM l){
    if(app.taskbarCreated&&msg==app.taskbarCreated){notification_cancel();app.trayAdded=0;tray_update(0);background_report();return 0;}
    switch(msg){
    case WM_CREATE:{app.window=window;const wchar_t *buttonNames[]={L"New chat",L"Rename",L"Delete",L"Pin / unpin",L"Import",L"Export"};
        for(int i=0;i<6;i++)app.controls[ID_NEW+i-ID_THREADS]=button(window,buttonNames[i],ID_NEW+i,0,0,0,0);
        app.controls[ID_THREADS-ID_THREADS]=child(window,L"LISTBOX",L"",WS_TABSTOP|LBS_NOTIFY|WS_VSCROLL|LBS_NOINTEGRALHEIGHT,ID_THREADS,0,0,0,0);
        app.controls[ID_MODELS-ID_THREADS]=child(window,L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,ID_MODELS,0,0,0,0);
        app.controls[ID_CHARACTERS-ID_THREADS]=child(window,L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,ID_CHARACTERS,0,0,0,0);
        int ids[]={ID_REFRESH,ID_ADD_MODEL,ID_EDIT_CHARACTER,ID_SETTINGS,ID_SEND,ID_STOP,ID_ADD_FOLDER,ID_MODEL_INFO,ID_MEMORIES,ID_SYNC,ID_WEB,ID_PHONE,ID_ACTIVITY};const wchar_t *names[]={L"Refresh",L"Add GGUF",L"Characters",L"Settings",L"Send",L"Stop",L"Add folder",L"Model info",L"Memories",L"Gabo net sync",L"Advanced workspace",L"Phone connection",L"Import activity"};for(int i=0;i<13;i++)app.controls[ids[i]-ID_THREADS]=button(window,names[i],ids[i],0,0,0,0);
        app.controls[ID_TRANSCRIPT-ID_THREADS]=child(window,MSFTEDIT_CLASS,L"",ES_MULTILINE|ES_READONLY|WS_VSCROLL,ID_TRANSCRIPT,0,0,0,0);SendMessageW(control(ID_TRANSCRIPT),EM_SETBKGNDCOLOR,0,BACK);SendMessageW(control(ID_TRANSCRIPT),EM_EXLIMITTEXT,0,4*1024*1024);RECT margins={scaled(18),scaled(15),0,0};(void)margins;
        app.controls[ID_PROMPT-ID_THREADS]=child(window,L"EDIT",L"",WS_TABSTOP|WS_VSCROLL|ES_MULTILINE|ES_AUTOVSCROLL|ES_WANTRETURN,ID_PROMPT,0,0,0,0);SendMessageW(control(ID_PROMPT),EM_LIMITTEXT,SPARK_TEXT,0);SetWindowSubclass(control(ID_PROMPT),input_proc,1,0);
        app.controls[ID_STATUS-ID_THREADS]=child(window,L"STATIC",L"Ready",SS_LEFT,ID_STATUS,0,0,0,0);SendMessageW(control(ID_STATUS),WM_SETFONT,(WPARAM)app.smallfont,TRUE);
        layout();render_threads();render_characters();render_transcript();render_draft();render_memories();busy(0);PostMessageW(window,WM_INITIALIZE,0,0);return 0;}
    case WM_INITIALIZE:{char error[512]={0};app.web=web_start(window,app.config,app.directory,error);if(*error)status(error);
#ifdef SPARK_TEST_HARNESS
        if(app.webTest&&app.web){cJSON *details=web_connection_details(app.web);char *text=cJSON_PrintUnformatted(details);DWORD n;WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),text,(DWORD)strlen(text),&n,NULL);WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),"\n",1,&n,NULL);SecureZeroMemory(text,strlen(text));free(text);cJSON_Delete(details);}
#endif
        tray_update(0);background_report();refresh();SetTimer(window,3,1000,NULL);return 0;}
    case WM_TRAY:if(LOWORD(l)==NIN_SELECT||LOWORD(l)==NIN_KEYSELECT||LOWORD(l)==WM_LBUTTONDBLCLK)show_main();else if(LOWORD(l)==WM_CONTEXTMENU)tray_menu();else if(LOWORD(l)==NIN_BALLOONSHOW||LOWORD(l)==NIN_BALLOONHIDE||LOWORD(l)==NIN_BALLOONTIMEOUT||LOWORD(l)==NIN_BALLOONUSERCLICK)notification_callback(LOWORD(l));return 0;
    case WM_SPARK_WEB:{WebCall *call=(WebCall*)l;call->response=web_action(call);return 0;}
    case WM_SIZE:if(control(ID_PROMPT))layout();return 0;
    case WM_GETMINMAXINFO:((MINMAXINFO*)l)->ptMinTrackSize.x=scaled(940);((MINMAXINFO*)l)->ptMinTrackSize.y=scaled(650);return 0;
    case WM_PAINT:{PAINTSTRUCT p;HDC dc=BeginPaint(window,&p);RECT r;GetClientRect(window,&r);FillRect(dc,&r,app.background);SetBkMode(dc,TRANSPARENT);SelectObject(dc,app.titlefont);SetTextColor(dc,CYAN);label(dc,scaled(18),scaled(22),L"Guilty Spark 343");SelectObject(dc,app.smallfont);SetTextColor(dc,RGB(142,164,184));label(dc,scaled(18),scaled(63),L"NATIVE WINDOWS  /  LOCAL AI");label(dc,scaled(18),scaled(99),L"THIS MODEL'S CHATS");label(dc,scaled(246),r.bottom-scaled(56),L"Ctrl+Enter sends. Enter adds a line.");EndPaint(window,&p);return 0;}
    case WM_DRAWITEM:drawbutton((DRAWITEMSTRUCT*)l);return TRUE;
    case WM_CTLCOLORBTN:return colors(WM_CTLCOLORSTATIC,w);
    case WM_CTLCOLORSTATIC:case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX:return colors(msg,w);
    case WM_COMMAND:{int id=LOWORD(w),event=HIWORD(w);
        if(id==ID_TRAY_OPEN){show_main();return 0;}
        if(id==ID_TRAY_NOTIFICATIONS){notification_toggle();return 0;}
        if(id==ID_ACTIVITY||id==ID_TRAY_ACTIVITY){activity_open();return 0;}
        if(id==ID_TRAY_EXIT){app.forceExit=1;PostMessageW(window,WM_CLOSE,0,0);return 0;}
        if(id==ID_TRAY_SYNC){sync_start(1);return 0;}
        if(id==ID_TRAY_BACKGROUND){cJSON *candidate=cJSON_Duplicate(app.config,1);cJSON_DeleteItemFromObjectCaseSensitive(candidate,"background");cJSON_AddBoolToObject(candidate,"background",!app.keepRunning);char error[512]={0};if(!save_json(app.configPath,candidate,error)){cJSON_Delete(candidate);show_main();errorbox(window,error);return 0;}cJSON_Delete(app.config);app.config=candidate;app.keepRunning=!app.keepRunning;if(!app.keepRunning)show_main();background_report();return 0;}
        if(id==ID_TRAY_STARTUP){wchar_t *path=allocate(32768*sizeof(wchar_t)),*exe=allocate(32768*sizeof(wchar_t));char error[512]={0};GetModuleFileNameW(NULL,exe,32768);int current=startup_path(path,error)?startup_status(path,app.directory,error):-1;if(current<0||!startup_set(path,exe,app.directory,!current,error)){show_main();errorbox(window,error);}else status(current?"Windows startup disabled.":"343 will start in the background when you sign in to Windows.");free(path);free(exe);return 0;}
        if(id==ID_TRAY_SETTINGS){show_main();if(!app.worker&&!app.webBusy&&IsWindowEnabled(window))editor_open(4);return 0;}
        if(id==ID_SEND){send_message();return 0;}if(id==ID_STOP){SetEvent(app.cancel);status("Stopping generation...");return 0;}if(app.worker||app.webBusy)return 0;
        if(id==ID_NEW)new_conversation();else if(id==ID_REFRESH)refresh();else if(id==ID_ADD_MODEL)add_model();else if(id==ID_ADD_FOLDER)add_folder();else if(id==ID_MODEL_INFO)model_info();else if(id==ID_IMPORT)import_file();else if(id==ID_EXPORT)export_file();
        else if(id==ID_SETTINGS)editor_open(1);else if(id==ID_EDIT_CHARACTER)editor_open(0);else if(id==ID_RENAME)editor_open(2);else if(id==ID_MEMORIES&&*str(app.state,"selectedModel"))editor_open(3);else if(id==ID_SYNC)editor_open(4);else if(id==ID_PHONE)editor_open(5);else if(id==ID_WEB){if(app.web)web_open(app.web);else status("The advanced workspace is unavailable. Check the web folder and listener configuration.");}
        else if(id==ID_DELETE&&MessageBoxW(window,L"Delete this conversation?",L"Guilty Spark 343",MB_YESNO|MB_ICONQUESTION)==IDYES){char error[512]={0};if(delete_conversation(app.state,str(app.state,"activeThreadId"),error)){render_threads();render_transcript();render_draft();save();}else status(error);}
        else if(id==ID_PIN){cJSON *t=thread();cJSON_ReplaceItemInObjectCaseSensitive(t,"pinned",cJSON_CreateBool(!cJSON_IsTrue(get(t,"pinned"))));render_threads();save();}
        else if(id==ID_THREADS&&event==LBN_SELCHANGE){LRESULT i=SendMessageW(control(id),LB_GETCURSEL,0,0);cJSON *t=(cJSON*)SendMessageW(control(id),LB_GETITEMDATA,(WPARAM)i,0);if(t&&(INT_PTR)t!=LB_ERR&&remember_draft()){char error[512]={0};if(select_thread(app.state,str(t,"id"),error)){render_transcript();render_draft();save();}else status(error);}render_threads();}
        else if(id==ID_MODELS&&event==CBN_SELCHANGE){cJSON *m=cJSON_GetArrayItem(app.models,(int)SendMessageW(control(id),CB_GETCURSEL,0,0));if(m){if(activate_model(str(m,"name"))){model_status();save();}else render_models();}}
        else if(id==ID_CHARACTERS&&event==CBN_SELCHANGE){int index=(int)SendMessageW(control(id),CB_GETCURSEL,0,0);cJSON *c=cJSON_GetArrayItem(get(app.state,"characters"),index-1);char error[512]={0};if(!select_character(app.state,index>0?str(c,"id"):"",error)){render_characters();status(error);}else if(save())character_status();}
        return 0;}
    case WM_DELTA:{char *text=(char*)l;if(app.reply){Buffer b={0};adds(&b,str(app.reply,"content"));adds(&b,text);setstr(app.reply,"content",b.p);free(b.p);append_display(text,0);if(app.smokeAction&&!app.smokeActionSent){app.smokeActionSent=1;PostMessageW(window,app.smokeAction==2?WM_CLOSE:WM_COMMAND,app.smokeAction==2?0:ID_STOP,0);}}free(text);return 0;}
    case WM_FINISHED:{Work *work=(Work*)l;if(app.worker){WaitForSingleObject(app.worker,INFINITE);CloseHandle(app.worker);app.worker=NULL;}busy(0);
        if(work->kind==0){if(work->ok){cJSON_Delete(app.models);app.models=work->models;work->models=NULL;render_models();model_status();save();}else status(work->error);}
        else {if(app.reply&&!*str(app.reply,"content"))cJSON_Delete(cJSON_DetachItemViaPointer(get(app.replyThread,"messages"),app.reply)),app.reply=NULL;
            if(*work->memoryError){char result[1100];snprintf(result,sizeof(result),"%s Memory: %s",work->ok?"Reply complete.":work->error,work->memoryError);status(result);}
            else status(work->ok?(work->memoryChanged?"Reply complete. Memories updated for this model.":"Reply complete. Saved locally."):work->error);
            save();if(app.smoke)smoke_result(work);app.reply=NULL;app.replyThread=NULL;}
        if(app.smoke&&!app.smokeStarted&&work->kind==0){app.smokeStarted=1;if(work->ok&&*str(app.state,"selectedModel")&&!cJSON_IsFalse(get(selected_model(),"supported"))){new_conversation();SetWindowTextW(control(ID_PROMPT),L"Write a short Python function named add that adds two numbers. Include a code block.");PostMessageW(window,WM_COMMAND,ID_SEND,0);}else{if(!*work->error)snprintf(work->error,512,"%s",*str(selected_model(),"reason")?str(selected_model(),"reason"):"No model is available.");smoke_result(work);}}
        cJSON_Delete(work->config);cJSON_Delete(work->state);cJSON_Delete(work->thread);cJSON_Delete(work->models);free(work);if(app.closing&&!app.syncWorker)DestroyWindow(window);return 0;}
    case WM_SYNC_READY:{SyncWork *work=(SyncWork*)l;if(app.syncWorker){WaitForSingleObject(app.syncWorker,INFINITE);CloseHandle(app.syncWorker);app.syncWorker=NULL;}app.syncReady=work;if(app.closing){cJSON_Delete(work->config);cJSON_Delete(work->source);free(work);app.syncReady=NULL;if(!app.worker)DestroyWindow(window);}else sync_apply();return 0;}
    case WM_TIMER:if(w==2){KillTimer(window,2);app.forceExit=1;PostMessageW(window,WM_CLOSE,0,0);}else if(w==3){if(app.openEvent&&WaitForSingleObject(app.openEvent,0)==WAIT_OBJECT_0)show_main();if(!app.trayAdded)tray_update(0);sync_apply();if(GetTickCount64()>=app.nextSync)sync_start(0);notification_tick();
#ifdef SPARK_TEST_HARNESS
        if(app.webTest){wchar_t path[32768];swprintf(path,32768,L"%ls\\close-web-test",app.directory);if(GetFileAttributesW(path)!=INVALID_FILE_ATTRIBUTES){app.forceExit=1;PostMessageW(window,WM_CLOSE,0,0);}}
#endif
    }return 0;
    case WM_QUERYENDSESSION:return TRUE;
    case WM_ENDSESSION:if(w){remember_draft();save();app.forceExit=1;SendMessageW(window,WM_CLOSE,0,0);}return 0;
    case WM_CLOSE:if(app.keepRunning&&!app.forceExit&&!app.smoke){hide_main();return 0;}if(!remember_draft())return 0;app.closing=1;SetEvent(app.syncCancel);if(app.worker||app.syncWorker){SetEvent(app.cancel);status("Finishing local work and saving...");return 0;}DestroyWindow(window);return 0;
    case WM_DESTROY:app.closing=1;tray_update(1);background_report();web_stop(app.web);app.web=NULL;runner_stop(&app.runner);save();PostQuitMessage(app.exitCode);return 0;
    }return DefWindowProcW(window,msg,w,l);
}

static const wchar_t *character_labels[]={L"Name",L"System instructions",L"Description / personality",L"Scenario / lore",L"Greeting for a new conversation"};
static void fill_character(Editor *e,cJSON *card){const char *keys[]={"name","systemPrompt","description","scenario","greeting"};for(int i=0;i<5;i++)caption(e->fields[i],str(card,keys[i]));if(card&&*str(card,"personality")){Buffer b={0};adds(&b,str(card,"description"));if(b.n)adds(&b,"\n\n");adds(&b,str(card,"personality"));caption(e->fields[2],b.p);free(b.p);}}
static LRESULT CALLBACK editor_proc(HWND window,UINT msg,WPARAM w,LPARAM l){Editor *e=(Editor*)GetWindowLongPtrW(window,GWLP_USERDATA);
    switch(msg){
    case WM_CREATE:{e=((CREATESTRUCTW*)l)->lpCreateParams;SetWindowLongPtrW(window,GWLP_USERDATA,(LONG_PTR)e);int y=18;
        if(e->kind==0){for(int i=0;i<5;i++){child(window,L"STATIC",character_labels[i],0,0,18,y,580,22);y+=24;e->fields[i]=child(window,L"EDIT",L"",WS_TABSTOP|ES_AUTOVSCROLL|(i?ES_MULTILINE|WS_VSCROLL:ES_AUTOHSCROLL),0,18,y,580,i?66:30);SendMessageW(e->fields[i],EM_LIMITTEXT,i?16000:200,0);y+=(i?66:30)+12;}fill_character(e,find_id(get(app.state,"characters"),e->id));button(window,L"New character",ID_EDITOR_NEW,18,y,130,34);button(window,L"Delete",ID_EDITOR_DELETE,158,y,92,34);}
        else if(e->kind==1){const wchar_t *labels[]={L"Inference backend",L"Loopback Ollama API address",L"llama-server.exe path",L"Context size",L"CPU threads",L"Response token limit",L"Temperature (0 to 2)",L"Your persona"};
            for(int i=0;i<8;i++){child(window,L"STATIC",labels[i],0,0,18,y,580,20);y+=21;if(!i){e->fields[i]=child(window,L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST,0,18,y,580,200);SendMessageW(e->fields[i],CB_ADDSTRING,0,(LPARAM)L"Bundled local GGUF engine");SendMessageW(e->fields[i],CB_ADDSTRING,0,(LPARAM)L"Ollama-compatible server");SendMessageW(e->fields[i],CB_SETCURSEL,!strcmp(str(app.config,"mode"),"ollama"),0);}else e->fields[i]=child(window,L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL|(i==7?ES_MULTILINE|WS_VSCROLL:0),0,18,y,580,i==7?65:28);y+=(i==7?65:28)+9;}
            caption(e->fields[1],str(app.config,"api"));caption(e->fields[2],str(app.config,"llamaServer"));char value[40];snprintf(value,40,"%d",get(app.config,"contextSize")->valueint);caption(e->fields[3],value);snprintf(value,40,"%d",get(app.config,"threads")->valueint);caption(e->fields[4],value);snprintf(value,40,"%d",get(get(app.state,"settings"),"numPredict")->valueint);caption(e->fields[5],value);snprintf(value,40,"%.2f",get(get(app.state,"settings"),"temperature")->valuedouble);caption(e->fields[6],value);caption(e->fields[7],str(app.state,"persona"));
        }else if(e->kind==3){
            HWND model=child(window,L"STATIC",L"",SS_LEFT,0,18,y,580,44);caption(model,str(app.state,"selectedModel"));y+=48;
            e->fields[1]=child(window,L"BUTTON",L"Use memories in this model's chats",WS_TABSTOP|BS_AUTOCHECKBOX,ID_MEMORY_ENABLED,18,y,580,26);y+=32;
            e->fields[2]=child(window,L"BUTTON",L"Automatically save clear facts and preferences I state",WS_TABSTOP|BS_AUTOCHECKBOX,ID_MEMORY_AUTO,18,y,580,26);y+=38;
            SetWindowTheme(e->fields[1],L"",L"");SetWindowTheme(e->fields[2],L"",L"");
            SendMessageW(e->fields[1],BM_SETCHECK,memory_option(app.state,"enabled")?BST_CHECKED:BST_UNCHECKED,0);SendMessageW(e->fields[2],BM_SETCHECK,memory_option(app.state,"automatic")?BST_CHECKED:BST_UNCHECKED,0);
            child(window,L"STATIC",L"One memory per line. Edit a line to correct it; remove a line to forget it.\r\nThese notes stay with this model across new chats.\r\nYou can also say: Remember that I prefer short answers.",SS_LEFT,0,18,y,580,64);y+=70;
            button(window,L"Learn from saved chats",ID_MEMORY_LEARN,18,y,230,32);y+=40;
            e->fields[3]=child(window,L"STATIC",L"Scan only your messages in this model's saved chats.\r\nReview the additions below, then Save. Cancel leaves memories unchanged.",SS_LEFT,ID_MEMORY_SCAN,18,y,580,48);y+=54;
            e->fields[0]=child(window,L"EDIT",L"",WS_TABSTOP|ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL|ES_WANTRETURN,ID_MEMORY_TEXT,18,y,580,190);SendMessageW(e->fields[0],EM_LIMITTEXT,SPARK_MEMORY_TOTAL+SPARK_MEMORY_ITEMS*2,0);char *notes=memory_text(app.state);caption(e->fields[0],notes);free(notes);y+=200;
            child(window,L"STATIC",L"Up to 32 notes, 512 bytes each, 4,096 bytes total.\r\nForgetting a note does not erase it from old chats or exported backups.",SS_LEFT,0,18,y,580,44);y+=52;
            button(window,L"Clear notes",ID_MEMORY_CLEAR,18,y,120,34);
        }else if(e->kind==5){
            cJSON *cfg=get(app.config,"web");e->fields[0]=child(window,L"BUTTON",L"Allow phone connections on my private network",WS_TABSTOP|BS_AUTOCHECKBOX,0,18,y,580,28);SendMessageW(e->fields[0],BM_SETCHECK,cJSON_IsTrue(get(cfg,"lan"))?BST_CHECKED:BST_UNCHECKED,0);y+=44;
            child(window,L"STATIC",L"Port (0 selects an available port; use a fixed port for a saved phone link)",0,0,18,y,580,22);y+=26;e->fields[1]=child(window,L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL,0,18,y,580,30);char port[24];snprintf(port,sizeof(port),"%d",get(cfg,"port")?get(cfg,"port")->valueint:0);caption(e->fields[1],port);y+=48;
            child(window,L"STATIC",L"Save to apply connection settings. This creates a new access code.\r\nConnect from your private Wi-Fi or encrypted mesh while 343 is open.\r\nIf Windows asks, allow this app on your private network.",SS_LEFT,0,18,y,580,72);y+=80;
            cJSON *details=web_connection_details(app.web);Buffer text={0};if(details){wchar_t host[256];DWORD n=256;GetComputerNameW(host,&n);char *name=utf8(host);char connection[1024];snprintf(connection,sizeof(connection),"Address: http://%s:%d/\r\nOr use this PC's private IPv4 address with port %d.\r\n\r\nAccess code (keep private):\r\n%s\r\n\r\nPhone access: %s",name,get(details,"port")->valueint,get(details,"port")->valueint,str(details,"token"),cJSON_IsTrue(get(details,"lan"))?"enabled":"disabled");free(name);adds(&text,connection);}else adds(&text,"The web listener is not running. Save to start it.");cJSON_Delete(details);
            e->fields[2]=child(window,L"EDIT",L"",WS_TABSTOP|ES_MULTILINE|ES_READONLY|WS_VSCROLL,0,18,y,580,190);caption(e->fields[2],text.p);if(text.p)SecureZeroMemory(text.p,text.n);free(text.p);y+=210;
        }else if(e->kind==4){
            cJSON *cfg=get(app.config,"gaboSync");e->fields[0]=child(window,L"BUTTON",L"Automatically import Gabo net chats while 343 is open",WS_TABSTOP|BS_AUTOCHECKBOX,0,18,y,580,28);SendMessageW(e->fields[0],BM_SETCHECK,cJSON_IsTrue(get(cfg,"enabled"))?BST_CHECKED:BST_UNCHECKED,0);y+=40;
            const wchar_t *labels[]={L"Gabo net address (including final /)",L"Browser profile folder (for example, Chrome User Data / Default)",L"Browser program (chrome.exe or msedge.exe)",L"Optional JSON source file (instead of the browser)",L"Check interval in seconds (15 to 3600)"};const char *keys[]={"origin","profile","browser","file","intervalSeconds"};
            for(int i=1;i<=5;i++){child(window,L"STATIC",labels[i-1],0,0,18,y,580,22);y+=24;e->fields[i]=child(window,L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL,0,18,y,580,28);if(i<5)caption(e->fields[i],str(cfg,keys[i-1]));else{char value[32];snprintf(value,sizeof(value),"%d",get(cfg,"intervalSeconds")?get(cfg,"intervalSeconds")->valueint:60);caption(e->fields[i],value);}y+=40;}
            child(window,L"STATIC",L"Reads an isolated copy of Gabo net's chats and generation labels.\r\nOnly verified model assignments are imported. Each model's\r\nautomatic-memory setting controls learning; uncertain chats stay pending.",SS_LEFT,0,18,y,580,68);y+=74;
            e->fields[6]=child(window,L"STATIC",L"",SS_LEFT,0,18,y,580,72);caption(e->fields[6],*app.syncStatus?app.syncStatus:"Save the source settings to enable automatic import.");y+=80;button(window,L"Save and sync now",ID_SYNC_NOW,18,y,200,34);
        }else{child(window,L"STATIC",L"Conversation name",0,0,18,y,580,22);y+=26;e->fields[0]=child(window,L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL,0,18,y,580,32);SendMessageW(e->fields[0],EM_LIMITTEXT,200,0);caption(e->fields[0],str(thread(),"name"));y+=50;}
        button(window,L"Cancel",ID_EDITOR_CANCEL,390,y,96,34);button(window,L"Save",ID_EDITOR_SAVE,502,y,96,34);RECT r={0,0,scaled(616),scaled(y+54)};AdjustWindowRectEx(&r,WS_CAPTION|WS_SYSMENU,FALSE,WS_EX_DLGMODALFRAME);SetWindowPos(window,NULL,0,0,r.right-r.left,r.bottom-r.top,SWP_NOMOVE|SWP_NOZORDER);return 0;}
    case WM_DRAWITEM:drawbutton((DRAWITEMSTRUCT*)l);return TRUE;
    case WM_CTLCOLORBTN:return colors(WM_CTLCOLORSTATIC,w);
    case WM_CTLCOLORSTATIC:case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX:return colors(msg,w);
    case WM_COMMAND:
        if(LOWORD(w)==ID_EDITOR_CANCEL){DestroyWindow(window);return 0;}
        if(LOWORD(w)==ID_MEMORY_CLEAR&&e->kind==3){SetWindowTextW(e->fields[0],L"");return 0;}
        if(LOWORD(w)==ID_MEMORY_LEARN&&e->kind==3){
            char *value=input(e->fields[0]),error[512]={0};cJSON *preview=cJSON_Duplicate(app.state,1);MemoryScan scan={0};
            int ok=set_memories(preview,value,SendMessageW(e->fields[1],BM_GETCHECK,0,0)==BST_CHECKED,SendMessageW(e->fields[2],BM_GETCHECK,0,0)==BST_CHECKED,error);free(value);
            if(ok)ok=learn_chat_memories(preview,&scan,error);
            if(ok){char *notes=memory_text(preview);caption(e->fields[0],notes);free(notes);char result[256];snprintf(result,sizeof(result),"%d chats, %d user messages: %d proposed additions.\r\nSkipped: %d conflicts, %d forgotten/over-limit facts. Review, then Save.",scan.conversations,scan.messages,scan.added,scan.conflicts,scan.skipped);caption(e->fields[3],result);}
            else errorbox(window,error);cJSON_Delete(preview);return 0;
        }
        if(LOWORD(w)==ID_EDITOR_NEW){e->id[0]=0;fill_character(e,NULL);return 0;}
        if(LOWORD(w)==ID_EDITOR_DELETE&&e->kind==0){cJSON *v=find_id(get(app.state,"characters"),e->id);if(v&&MessageBoxW(window,L"Delete this character? Models using it will return to Default assistant.",L"Guilty Spark 343",MB_YESNO|MB_ICONQUESTION)==IDYES){delete_character(app.state,e->id);save();render_characters();DestroyWindow(window);}return 0;}
        if(LOWORD(w)==ID_EDITOR_SAVE||LOWORD(w)==ID_SYNC_NOW){
            if(e->kind==0){cJSON *v=cJSON_CreateObject();const char *keys[]={"name","systemPrompt","description","scenario","greeting"};for(int i=0;i<5;i++){char *s=input(e->fields[i]);if(strlen(s)>(i?SPARK_TEXT:200)){free(s);cJSON_Delete(v);errorbox(window,"Character text exceeds its UTF-8 size limit.");return 0;}setstr(v,keys[i],s);free(s);}if(!*str(v,"name")){cJSON_Delete(v);errorbox(window,"A character name is required.");return 0;}
                cJSON *map=get(app.state,"modelCharacters");if(*str(app.state,"selectedModel")&&!get(map,str(app.state,"selectedModel"))&&cJSON_GetArraySize(map)>=SPARK_MODEL_CHARACTERS){cJSON_Delete(v);errorbox(window,"Model personality storage is full. Set an unused model to Default assistant first.");return 0;}
                if(!*e->id){if(cJSON_GetArraySize(get(app.state,"characters"))>=200){cJSON_Delete(v);errorbox(window,"Character limit reached.");return 0;}char id[64];newid(id);strcpy(e->id,id);}setstr(v,"id",e->id);setstr(v,"personality","");cJSON *cards=get(app.state,"characters"),*old=find_id(cards,e->id);if(old)cJSON_ReplaceItemViaPointer(cards,old,v);else cJSON_AddItemToArray(cards,v);char error[512]={0};if(!select_character(app.state,e->id,error)){errorbox(window,error);return 0;}render_characters();
            }else if(e->kind==1){char *values[8]={0};for(int i=1;i<8;i++)values[i]=input(e->fields[i]);char *end;double temp=strtod(values[6],&end);int valid=!*end&&*values[6]&&temp>=0&&temp<=2;long numbers[3];for(int i=0;i<3;i++){numbers[i]=strtol(values[i+3],&end,10);valid=valid&&!*end&&*values[i+3];}valid=valid&&numbers[0]>=128&&numbers[0]<=131072&&numbers[1]>=1&&numbers[1]<=256&&numbers[2]>=1&&numbers[2]<=32768;
                if(!valid||strlen(values[7])>SPARK_TEXT){for(int i=1;i<8;i++)free(values[i]);errorbox(window,"Check context (128-131072), threads (1-256), tokens (1-32768), temperature (0-2), and persona size (50000 UTF-8 bytes).");return 0;}
                runner_stop(&app.runner);setstr(app.config,"mode",SendMessageW(e->fields[0],CB_GETCURSEL,0,0)==1?"ollama":"local");setstr(app.config,"api",values[1]);setstr(app.config,"llamaServer",values[2]);setnum(app.config,"contextSize",numbers[0]);setnum(app.config,"threads",numbers[1]);setnum(get(app.state,"settings"),"numPredict",numbers[2]);setnum(get(app.state,"settings"),"temperature",temp);setstr(app.state,"persona",values[7]);for(int i=1;i<8;i++)free(values[i]);save_config();
            }else if(e->kind==3){
                char *value=input(e->fields[0]),error[512]={0};cJSON *previous=cJSON_Duplicate(get(app.state,"modelMemories"),1);
                int ok=set_memories(app.state,value,SendMessageW(e->fields[1],BM_GETCHECK,0,0)==BST_CHECKED,SendMessageW(e->fields[2],BM_GETCHECK,0,0)==BST_CHECKED,error);free(value);
                if(!ok){cJSON_Delete(previous);errorbox(window,error);return 0;}
                if(!save_json(app.statePath,app.state,error)){cJSON_ReplaceItemInObjectCaseSensitive(app.state,"modelMemories",previous);errorbox(window,error);return 0;}
                cJSON_Delete(previous);render_memories();status("Memories and preferences saved for this model.");DestroyWindow(window);return 0;
            }else if(e->kind==5){
                char *value=input(e->fields[1]),*end;long port=strtol(value,&end,10);int valid=*value&&!*end&&port>=0&&port<=65535;free(value);if(!valid){errorbox(window,"Enter a port from 0 to 65535.");return 0;}
                cJSON *candidate=cJSON_Duplicate(app.config,1),*cfg=get(candidate,"web");cJSON_DeleteItemFromObjectCaseSensitive(cfg,"enabled");cJSON_AddBoolToObject(cfg,"enabled",1);cJSON_DeleteItemFromObjectCaseSensitive(cfg,"lan");cJSON_AddBoolToObject(cfg,"lan",SendMessageW(e->fields[0],BM_GETCHECK,0,0)==BST_CHECKED);setnum(cfg,"port",port);char error[512]={0};if(!save_json(app.configPath,candidate,error)){cJSON_Delete(candidate);errorbox(window,error);return 0;}cJSON_Delete(app.config);app.config=candidate;web_stop(app.web);app.web=web_start(app.window,app.config,app.directory,error);DestroyWindow(window);if(!app.web)errorbox(app.window,error);else editor_open(5);return 0;
            }else if(e->kind==4){
                cJSON *cfg=cJSON_IsObject(get(app.config,"gaboSync"))?cJSON_Duplicate(get(app.config,"gaboSync"),1):cJSON_CreateObject();const char *keys[]={"origin","profile","browser","file"};for(int i=1;i<=4;i++){char *text=input(e->fields[i]);setstr(cfg,keys[i-1],text);free(text);}char *value=input(e->fields[5]),*end;long seconds=strtol(value,&end,10);int valid=*value&&!*end&&seconds>=15&&seconds<=3600;free(value);
                if(!valid||!*str(cfg,"origin")||(!*str(cfg,"file")&&(!*str(cfg,"profile")||!*str(cfg,"browser")))){cJSON_Delete(cfg);errorbox(window,"Enter a Gabo net address, a browser/profile or JSON file, and an interval from 15 to 3600 seconds.");return 0;}
                setnum(cfg,"intervalSeconds",seconds);cJSON_DeleteItemFromObjectCaseSensitive(cfg,"enabled");cJSON_AddBoolToObject(cfg,"enabled",SendMessageW(e->fields[0],BM_GETCHECK,0,0)==BST_CHECKED);cJSON *candidate=cJSON_Duplicate(app.config,1);cJSON_DeleteItemFromObjectCaseSensitive(candidate,"gaboSync");cJSON_AddItemToObject(candidate,"gaboSync",cfg);char error[512]={0};if(!save_json(app.configPath,candidate,error)){cJSON_Delete(candidate);errorbox(window,error);return 0;}cJSON_Delete(app.config);app.config=candidate;app.nextSync=0;int immediate=LOWORD(w)==ID_SYNC_NOW;DestroyWindow(window);if(immediate)sync_start(1);return 0;
            }else{char *value=input(e->fields[0]);if(strlen(value)>200){free(value);errorbox(window,"Conversation names are limited to 200 UTF-8 bytes.");return 0;}if(*value)setstr(thread(),"name",value);free(value);render_threads();}
            int saved=save();if(saved&&e->kind==0)character_status();int refresh_needed=e->kind==1;DestroyWindow(window);if(refresh_needed)refresh();return 0;
        }break;
    case WM_CLOSE:DestroyWindow(window);return 0;
    case WM_DESTROY:EnableWindow(app.window,TRUE);SetForegroundWindow(app.window);free(e);return 0;
    }return DefWindowProcW(window,msg,w,l);
}
static void editor_open(int kind){Editor *e=allocate(sizeof(*e));e->kind=kind;if(!kind)snprintf(e->id,sizeof(e->id),"%s",str(app.state,"activeCharacterId"));EnableWindow(app.window,FALSE);RECT parent;GetWindowRect(app.window,&parent);HWND h=CreateWindowExW(WS_EX_DLGMODALFRAME,L"SparkEditor",kind==1?L"Settings and persona":kind==2?L"Rename conversation":kind==3?L"Memories for this model":kind==4?L"Gabo net automatic import":kind==5?L"Phone and web connection":L"Characters",WS_CAPTION|WS_SYSMENU,parent.left+scaled(100),parent.top+scaled(20),scaled(620),scaled(600),app.window,NULL,app.instance,e);ShowWindow(h,IsWindowVisible(app.window)?SW_SHOW:SW_HIDE);}
static void resolve_path(cJSON *obj,const char *key,const wchar_t *base){const char *s=str(obj,key);if(!*s)return;wchar_t *w=wide(s),joinedPath[32768],absolute[32768];if(!w)return;if(w[0]==L'\\'||w[0]==L'/'||(w[0]&&w[1]==L':'))swprintf(joinedPath,32768,L"%ls",w);else swprintf(joinedPath,32768,L"%ls\\%ls",base,w);DWORD n=GetFullPathNameW(joinedPath,32768,absolute,NULL);if(n&&n<32768){char *u=utf8(absolute);setstr(obj,key,u);free(u);}free(w);}
static cJSON *configuration(const wchar_t *file,const wchar_t *exeDir,char error[512]){
    cJSON *config=cJSON_CreateObject();setstr(config,"mode","local");setstr(config,"api","http://127.0.0.1:11434");setstr(config,"llamaServer","runtime/llama-server.exe");setnum(config,"contextSize",2048);setnum(config,"threads",4);setnum(config,"gpuLayers",0);setstr(config,"kvCacheType","f16");setstr(config,"embeddingModel","");cJSON_AddArrayToObject(config,"models");cJSON_AddArrayToObject(config,"modelFolders");cJSON_AddObjectToObject(config,"gaboSync");cJSON *web=cJSON_AddObjectToObject(config,"web");cJSON_AddBoolToObject(web,"enabled",1);setstr(web,"root","web");resolve_path(web,"root",exeDir);resolve_path(config,"llamaServer",exeDir);
    cJSON_AddBoolToObject(config,"background",0);
    wchar_t defaultFolder[32768];swprintf(defaultFolder,32768,L"%ls\\models",exeDir);folder_add(config,defaultFolder);PWSTR profile=NULL;if(SUCCEEDED(SHGetKnownFolderPath(&FOLDERID_Profile,0,NULL,&profile))){swprintf(defaultFolder,32768,L"%ls\\.ollama\\models",profile);folder_add(config,defaultFolder);CoTaskMemFree(profile);}DWORD envLength=GetEnvironmentVariableW(L"OLLAMA_MODELS",defaultFolder,32768);if(envLength&&envLength<32768)folder_add(config,defaultFolder);
    if(file&&*file){cJSON *in=load_json(file,error);if(!in){cJSON_Delete(config);return NULL;}const char *keys[]={"mode","api","llamaServer","contextSize","threads","gpuLayers","models","modelFolders","gaboSync","web","kvCacheType","embeddingModel","background"};for(unsigned i=0;i<13;i++)if(get(in,keys[i]))cJSON_ReplaceItemInObjectCaseSensitive(config,keys[i],cJSON_Duplicate(get(in,keys[i]),1));cJSON_Delete(in);
        wchar_t base[32768];GetFullPathNameW(file,32768,base,NULL);wchar_t *slash=wcsrchr(base,L'\\');if(slash)*slash=0;resolve_path(config,"llamaServer",base);resolve_path(config,"embeddingModel",base);resolve_path(get(config,"web"),"root",base);cJSON *m;cJSON_ArrayForEach(m,get(config,"models"))resolve_path(m,"path",base);
        cJSON *f;cJSON_ArrayForEach(f,get(config,"modelFolders"))if(cJSON_IsString(f)){cJSON *holder=cJSON_CreateObject();setstr(holder,"path",f->valuestring);resolve_path(holder,"path",base);cJSON_SetValuestring(f,str(holder,"path"));cJSON_Delete(holder);}
    }
    if((strcmp(str(config,"mode"),"local")&&strcmp(str(config,"mode"),"ollama"))||!cJSON_IsArray(get(config,"models"))||cJSON_GetArraySize(get(config,"models"))>128||!cJSON_IsArray(get(config,"modelFolders"))||cJSON_GetArraySize(get(config,"modelFolders"))>32){strcpy(error,"Invalid backend mode, model collection or folder list.");cJSON_Delete(config);return NULL;}
    cJSON *f;cJSON_ArrayForEach(f,get(config,"modelFolders"))if(!cJSON_IsString(f)||!f->valuestring[0]){strcpy(error,"Model folders must be nonempty paths.");cJSON_Delete(config);return NULL;}
    cJSON *m;cJSON_ArrayForEach(m,get(config,"models"))if(!cJSON_IsObject(m)||!*str(m,"name")||!*str(m,"path")){strcpy(error,"Each local model needs a name and file path.");cJSON_Delete(config);return NULL;}
    cJSON_ArrayForEach(m,get(config,"models")){wchar_t *path=wide(str(m,"path"));if(path){wchar_t *end=wcsrchr(path,L'\\');if(end){*end=0;size_t n=wcslen(path);if(n>6&&!_wcsicmp(path+n-6,L"\\blobs")){path[n-6]=0;folder_add(config,path);}}free(path);}}
    return config;
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE previous,PWSTR command,int show){
    (void)previous;(void)command;app.instance=instance;app.dpi=96;HDC screen=GetDC(NULL);app.dpi=GetDeviceCaps(screen,LOGPIXELSX);ReleaseDC(NULL,screen);OleInitialize(NULL);
    wchar_t exeDir[32768],configFile[32768]=L"";GetModuleFileNameW(NULL,exeDir,32768);wchar_t *slash=wcsrchr(exeDir,L'\\');if(slash)*slash=0;
    PWSTR local=NULL;if(SUCCEEDED(SHGetKnownFolderPath(&FOLDERID_LocalAppData,0,NULL,&local))){swprintf(app.directory,32768,L"%ls\\GuiltySpark343",local);CoTaskMemFree(local);}else return 1;
    int argc;wchar_t **argv=CommandLineToArgvW(GetCommandLineW(),&argc);for(int i=1;i<argc;i++){
        if(!wcscmp(argv[i],L"--config")&&i+1<argc)wcsncpy(configFile,argv[++i],32767);
        else if(!wcscmp(argv[i],L"--data-dir")&&i+1<argc){GetFullPathNameW(argv[++i],32768,app.directory,NULL);
#ifdef SPARK_TEST_HARNESS
            app.explicitData=1;
#endif
        }
#ifdef SPARK_TEST_HARNESS
        else if(!wcscmp(argv[i],L"--web-test"))app.webTest=1;
#endif
        else if(!wcscmp(argv[i],L"--receipt")&&i+1<argc)wcsncpy(app.receiptPath,argv[++i],32767);
        else if(!wcscmp(argv[i],L"--model")&&i+1<argc){free(app.requestedModel);app.requestedModel=utf8(argv[++i]);}
        else if(!wcscmp(argv[i],L"--list-models"))app.listOnly=1;
        else if(!wcscmp(argv[i],L"--sync-once"))app.syncOnce=1;
        else if(!wcscmp(argv[i],L"--background"))app.startHidden=1;
        else if(!wcscmp(argv[i],L"--startup-on"))app.startupAction=1;
        else if(!wcscmp(argv[i],L"--startup-off"))app.startupAction=-1;
        else if(!wcscmp(argv[i],L"--smoke"))app.smoke=1;
        else if(!wcscmp(argv[i],L"--smoke-stop")){app.smoke=1;app.smokeAction=1;}
        else if(!wcscmp(argv[i],L"--smoke-close")){app.smoke=1;app.smokeAction=2;}
        else{MessageBoxW(NULL,L"Options: --config file.json --data-dir folder --background --startup-on --startup-off --sync-once --smoke --receipt file.json",L"Guilty Spark 343",MB_OK);LocalFree(argv);return 1;}
    }LocalFree(argv);if((app.smoke||app.listOnly||app.syncOnce)&&!*app.receiptPath)return 1;
#ifdef SPARK_TEST_HARNESS
    if(app.webTest&&!app.explicitData)return 1;
#endif
    if(SHCreateDirectoryExW(NULL,app.directory,NULL)!=ERROR_SUCCESS&&GetFileAttributesW(app.directory)==INVALID_FILE_ATTRIBUTES){errorbox(NULL,"Cannot create application data directory.");return 1;}
    if(app.startupAction){char error[512]={0};wchar_t *path=allocate(32768*sizeof(wchar_t)),*exe=allocate(32768*sizeof(wchar_t));GetModuleFileNameW(NULL,exe,32768);int ok=startup_path(path,error)&&startup_set(path,exe,app.directory,app.startupAction>0,error);if(*app.receiptPath){cJSON *receipt=cJSON_CreateObject();cJSON_AddBoolToObject(receipt,"passed",ok);cJSON_AddBoolToObject(receipt,"startupEnabled",ok&&app.startupAction>0);setstr(receipt,"error",error);if(!save_json(app.receiptPath,receipt,error))ok=0;cJSON_Delete(receipt);}else if(!ok)errorbox(NULL,error);free(path);free(exe);OleUninitialize();return !ok;}
    wchar_t eventName[100];if(!background_key(app.directory,eventName)){errorbox(NULL,"Cannot identify this workspace.");return 1;}
    wchar_t lockPath[32768];swprintf(lockPath,32768,L"%ls\\instance.lock",app.directory);app.lock=CreateFileW(lockPath,GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_ALWAYS,FILE_FLAG_DELETE_ON_CLOSE,NULL);if(app.lock==INVALID_HANDLE_VALUE){
        if(GetLastError()==ERROR_SHARING_VIOLATION&&!app.listOnly&&!app.syncOnce&&!app.smoke){HANDLE existing=OpenEventW(EVENT_MODIFY_STATE,FALSE,eventName);if(existing){if(!app.startHidden)SetEvent(existing);CloseHandle(existing);OleUninitialize();return 0;}if(app.startHidden){OleUninitialize();return 0;}}
        errorbox(NULL,"This workspace is already open in another application instance.");return 1;
    }
    swprintf(app.statePath,32768,L"%ls\\workspace.json",app.directory);swprintf(app.configPath,32768,L"%ls\\settings.json",app.directory);
    wchar_t portableConfig[32768];swprintf(portableConfig,32768,L"%ls\\guilty-spark.json",exeDir);
    char error[512]={0};const wchar_t *selected=*configFile?configFile:GetFileAttributesW(app.configPath)!=INVALID_FILE_ATTRIBUTES?app.configPath:GetFileAttributesW(portableConfig)!=INVALID_FILE_ATTRIBUTES?portableConfig:NULL;app.config=configuration(selected,exeDir,error);if(!app.config){errorbox(NULL,error);CloseHandle(app.lock);return 1;}
    if(app.listOnly){cJSON *models=list_models(app.config,NULL,error),*receipt=cJSON_CreateObject();cJSON_AddBoolToObject(receipt,"passed",models!=NULL);setstr(receipt,"error",error);cJSON_AddItemToObject(receipt,"models",models?models:cJSON_CreateArray());int ok=save_json(app.receiptPath,receipt,error)&&models!=NULL;cJSON_Delete(receipt);cJSON_Delete(app.config);CloseHandle(app.lock);free(app.requestedModel);OleUninitialize();return !ok;}
    if(GetFileAttributesW(app.statePath)!=INVALID_FILE_ATTRIBUTES){cJSON *stored=load_json(app.statePath,error);if(stored){char *json=cJSON_PrintUnformatted(stored);app.state=import_state(json,NULL,error);free(json);cJSON_Delete(stored);}if(!app.state){errorbox(NULL,"Saved workspace could not be read. It was preserved. Restore workspace.json.bak or choose a different --data-dir.");CloseHandle(app.lock);cJSON_Delete(app.config);return 1;}}
    else app.state=new_state();check_load();app.notices=cJSON_CreateArray();if(app.requestedModel){if(!select_model(app.state,app.requestedModel,error)){errorbox(NULL,error);return 1;}free(app.requestedModel);app.requestedModel=NULL;}app.models=cJSON_CreateArray();app.cancel=CreateEventW(NULL,TRUE,FALSE,NULL);app.syncCancel=CreateEventW(NULL,TRUE,FALSE,NULL);
    if(app.syncOnce){
        cJSON *cfg=get(app.config,"gaboSync"),*source=NULL;GaboScan scan={0};
        if(*str(cfg,"file")){wchar_t *file=wide(str(cfg,"file"));source=load_json(file,error);free(file);}else source=gabo_browser_snapshot(cfg,app.directory,app.syncCancel,error);
        cJSON *aliases=sync_aliases(cfg),*updated=source?gabo_merge(app.state,source,str(cfg,"origin"),aliases,&scan,error):NULL;int ok=updated&&save_json(app.statePath,updated,error);
        app.syncSucceeded=ok;if(ok)sync_report(&scan);else snprintf(app.syncStatus,sizeof(app.syncStatus),"Gabo net sync: %.450s",error);check_complete();
        cJSON *receipt=cJSON_CreateObject();cJSON_AddBoolToObject(receipt,"passed",ok);setstr(receipt,"error",error);setnum(receipt,"addedConversations",scan.added);setnum(receipt,"updatedConversations",scan.updated);setnum(receipt,"messages",scan.messages);setnum(receipt,"memories",scan.memories);setnum(receipt,"pending",scan.pending);setnum(receipt,"conflicts",scan.conflicts);setnum(receipt,"skipped",scan.skipped);cJSON_AddBoolToObject(receipt,"changed",scan.changed);
        if(!save_json(app.receiptPath,receipt,error))ok=0;cJSON_Delete(receipt);cJSON_Delete(updated);cJSON_Delete(aliases);cJSON_Delete(source);cJSON_Delete(app.state);cJSON_Delete(app.config);cJSON_Delete(app.models);cJSON_Delete(app.notices);cJSON_Delete(app.syncCheck);CloseHandle(app.cancel);CloseHandle(app.syncCancel);CloseHandle(app.lock);OleUninitialize();return !ok;
    }
    app.openEvent=CreateEventW(NULL,FALSE,FALSE,eventName);app.taskbarCreated=RegisterWindowMessageW(L"TaskbarCreated");app.keepRunning=app.startHidden||cJSON_IsTrue(get(app.config,"background"));
    app.font=CreateFontW(-MulDiv(11,app.dpi,72),0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");app.titlefont=CreateFontW(-MulDiv(19,app.dpi,72),0,0,0,FW_SEMIBOLD,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");app.smallfont=CreateFontW(-MulDiv(9,app.dpi,72),0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");app.background=CreateSolidBrush(BACK);app.panel=CreateSolidBrush(PANEL);
    INITCOMMONCONTROLSEX common={sizeof(common),ICC_STANDARD_CLASSES};InitCommonControlsEx(&common);HMODULE rich=LoadLibraryW(L"Msftedit.dll");if(!rich){errorbox(NULL,"Windows Rich Edit is unavailable.");return 1;}
    WNDCLASSEXW cls={sizeof(cls)};cls.hInstance=instance;cls.lpfnWndProc=window_proc;cls.lpszClassName=L"GuiltySpark343";cls.hCursor=LoadCursorW(NULL,IDC_ARROW);cls.hIcon=LoadIconW(NULL,IDI_APPLICATION);cls.hbrBackground=app.background;RegisterClassExW(&cls);cls.lpfnWndProc=editor_proc;cls.lpszClassName=L"SparkEditor";RegisterClassExW(&cls);cls.lpfnWndProc=activity_proc;cls.lpszClassName=L"SparkActivity";RegisterClassExW(&cls);
    HWND window=CreateWindowExW(0,L"GuiltySpark343",L"Guilty Spark 343",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,scaled(1160),scaled(790),NULL,NULL,instance,NULL);if(!window)return 1;BOOL dark=TRUE;DwmSetWindowAttribute(window,20,&dark,sizeof(dark));if(!app.startHidden)ShowWindow(window,app.smoke?SW_SHOWNOACTIVATE:show);UpdateWindow(window);
    MSG msg;while(GetMessageW(&msg,NULL,0,0)>0){if(!IsDialogMessageW(GetActiveWindow(),&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
    if(app.syncReady){cJSON_Delete(app.syncReady->source);cJSON_Delete(app.syncReady->config);free(app.syncReady);}if(app.openEvent)CloseHandle(app.openEvent);CloseHandle(app.syncCancel);CloseHandle(app.cancel);CloseHandle(app.lock);cJSON_Delete(app.state);cJSON_Delete(app.config);cJSON_Delete(app.models);cJSON_Delete(app.notices);cJSON_Delete(app.noticeActive);cJSON_Delete(app.noticeLast);cJSON_Delete(app.syncCheck);DeleteObject(app.font);DeleteObject(app.titlefont);DeleteObject(app.smallfont);DeleteObject(app.background);DeleteObject(app.panel);FreeLibrary(rich);OleUninitialize();return app.exitCode;
}

