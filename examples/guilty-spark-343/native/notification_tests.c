#define _CRT_SECURE_NO_WARNINGS
#include "spark.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){printf("FAIL notifications:%d: %s\n",__LINE__,#x);goto fail;}passed++;}while(0)
int notification_tests(void){
    int passed=0;cJSON *config=cJSON_Parse("{\"gaboSync\":{}}"),*pending=cJSON_CreateArray(),*event=cJSON_Parse("{\"models\":[{\"model\":\"alpha:latest\",\"added\":1,\"learned\":[\"private fact fingerprint\"]},{\"model\":\"beta:latest\",\"added\":0,\"learned\":[\"first\",\"second\"]},{\"model\":\"gamma:latest\",\"added\":0,\"updated\":1,\"learned\":[]}]}"),*extra=NULL;wchar_t title[64],body[256];char *encoded=NULL;
    CHECK(import_notifications_enabled(config));cJSON_AddBoolToObject(get(config,"gaboSync"),"notifications",0);CHECK(!import_notifications_enabled(config));cJSON_ReplaceItemInObjectCaseSensitive(get(config,"gaboSync"),"notifications",cJSON_CreateBool(1));CHECK(import_notifications_enabled(config));
    CHECK(queue_import_notifications(pending,event)==2);CHECK(cJSON_GetArraySize(pending)==2);cJSON *a=pending->child,*b=a->next;CHECK(!strcmp(str(a,"model"),"alpha:latest")&&get(a,"chats")->valueint==1&&get(a,"memories")->valueint==1);CHECK(!strcmp(str(b,"model"),"beta:latest")&&get(b,"chats")->valueint==0&&get(b,"memories")->valueint==2);
    import_notification_text(a,title,body);CHECK(wcsstr(title,L"Guilty Spark 343")&&wcsstr(body,L"alpha:latest")&&wcsstr(body,L"1 new chat, 1 new memory."));CHECK(!wcsstr(body,L"private fact")&&!wcsstr(body,L"beta:latest"));
    import_notification_text(b,title,body);CHECK(wcsstr(body,L"beta:latest")&&wcsstr(body,L"2 new memories.")&&!wcsstr(body,L"new chat"));
    extra=cJSON_Parse("{\"models\":[{\"model\":\"alpha:latest\",\"added\":2,\"learned\":[\"one\",\"two\"]}]}");CHECK(queue_import_notifications(pending,extra)==1);CHECK(cJSON_GetArraySize(pending)==2&&get(a,"chats")->valueint==3&&get(a,"memories")->valueint==3&&get(b,"memories")->valueint==2);import_notification_text(a,title,body);CHECK(wcsstr(body,L"3 new chats, 3 new memories."));cJSON_Delete(extra);
    extra=cJSON_Parse("{\"models\":[{\"model\":\"\",\"added\":1},{\"model\":\"gamma\",\"added\":0,\"learned\":[]}]}");CHECK(!queue_import_notifications(pending,extra)&&cJSON_GetArraySize(pending)==2);cJSON_Delete(extra);extra=NULL;
    setnum(a,"memories",0);import_notification_text(a,title,body);CHECK(wcsstr(body,L"3 new chats.")&&!wcsstr(body,L"new memories"));
    // A long Unicode model name cannot overflow the Windows notification fields
    // or split an emoji; line breaks in a label cannot forge a second summary.
    Buffer label={0};for(int i=0;i<95;i++)adds(&label,"a");adds(&label,"\xf0\x9f\x8c\x9f");for(int i=0;i<100;i++)adds(&label,"b");setstr(a,"model",label.p);free(label.p);import_notification_text(a,title,body);CHECK(wcslen(title)<64&&wcslen(body)<256&&wcsstr(body,L"...")&&wcsstr(body,L"3 new chats."));encoded=utf8(body);CHECK(encoded!=NULL);free(encoded);encoded=NULL;
    setstr(a,"model","Model\r\nInjected\tlabel");import_notification_text(a,title,body);CHECK(wcsstr(body,L"Model  Injected label\n3 new chats.")!=NULL);
    cJSON_AddBoolToObject(a,"test",1);import_notification_text(a,title,body);CHECK(wcsstr(title,L"notification test")&&wcsstr(body,L"Future Gabo net alerts")&&!wcsstr(body,L"3 new chats"));
    cJSON_Delete(pending);cJSON_Delete(config);cJSON_Delete(event);return passed;
fail:free(encoded);cJSON_Delete(extra);cJSON_Delete(pending);cJSON_Delete(config);cJSON_Delete(event);return -1;
}
