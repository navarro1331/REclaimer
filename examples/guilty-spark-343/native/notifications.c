#define _CRT_SECURE_NO_WARNINGS
#include "spark.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

int import_notifications_enabled(const cJSON *config){return !cJSON_IsFalse(get(get(config,"gaboSync"),"notifications"));}
// The caller supplies only a newly committed import receipt, never saved
// history at startup. Coalesce pending results by the exact owning model.
int queue_import_notifications(cJSON *pending,const cJSON *event){
    int count=0;cJSON *row;cJSON_ArrayForEach(row,get(event,"models")){
        int chats=get(row,"added")?get(row,"added")->valueint:0,memories=cJSON_GetArraySize(get(row,"learned"));
        const char *model=str(row,"model");if(!*model||(chats<=0&&memories<=0))continue;
        cJSON *notice=NULL,*n;cJSON_ArrayForEach(n,pending)if(!strcmp(str(n,"model"),model)&&!cJSON_IsTrue(get(n,"test"))){notice=n;break;}
        if(!notice){notice=cJSON_CreateObject();setstr(notice,"model",model);setnum(notice,"chats",0);setnum(notice,"memories",0);cJSON_AddItemToArray(pending,notice);}
        setnum(notice,"chats",get(notice,"chats")->valuedouble+chats);setnum(notice,"memories",get(notice,"memories")->valuedouble+memories);count++;
    }return count;
}
void import_notification_text(const cJSON *notice,wchar_t title[64],wchar_t body[256]){
    title[0]=body[0]=0;
    if(cJSON_IsTrue(get(notice,"test"))){wcscpy(title,L"Guilty Spark 343 - notification test");wcscpy(body,L"Notifications are enabled in 343.\nFuture Gabo net alerts will name each model and show its new chat and memory counts.\nClick to open Import activity.");return;}
    wcscpy(title,L"Guilty Spark 343 - Gabo net");wchar_t *model=wide(str(notice,"model"));if(!model)return;
    // Leave room for counts and the action. Never split a UTF-16 surrogate pair.
    size_t length=wcslen(model),n=length>96?96:length;if(n&&model[n-1]>=0xd800&&model[n-1]<=0xdbff)n--;
    wmemcpy(body,model,n);for(size_t i=0;i<n;i++)if(body[i]<32)body[i]=L' ';body[n]=0;if(length>n)wcscat(body,L"...");free(model);
    double chats=get(notice,"chats")?get(notice,"chats")->valuedouble:0,memories=get(notice,"memories")?get(notice,"memories")->valuedouble:0;
    wchar_t counts[120];if(chats&&memories)swprintf(counts,120,L"\n%.0f new %ls, %.0f new %ls.",chats,chats==1?L"chat":L"chats",memories,memories==1?L"memory":L"memories");
    else if(chats)swprintf(counts,120,L"\n%.0f new %ls.",chats,chats==1?L"chat":L"chats");else swprintf(counts,120,L"\n%.0f new %ls.",memories,memories==1?L"memory":L"memories");
    wcsncat(body,counts,255-wcslen(body));wcsncat(body,L"\nClick to review Import activity.",255-wcslen(body));
}
