#define _CRT_SECURE_NO_WARNINGS
#include "spark.h"
#include <string.h>
#include <stdlib.h>

static const char *variable(const char *token,const char *system,const char *prompt,const char *response){
    if(!strcmp(token,".System"))return system;if(!strcmp(token,".Prompt"))return prompt;if(!strcmp(token,".Response"))return response;return NULL;
}
static int turn(Buffer *out,const char *format,const char *system,const char *prompt,const char *response,int final,int *usesPrompt){
    struct {int parent,condition,otherwise;} stack[16];unsigned depth=0;int active=1;const char *p=format,*open;
    while((open=strstr(p,"{{"))){if(active&&!append(out,p,(size_t)(open-p)))return 0;const char *end=strstr(open+2,"}}");if(!end)return 0;
        const char *start=open+2;while(start<end&&(*start==' '||*start=='-'||*start=='\t'||*start=='\n'))start++;const char *last=end;while(last>start&&(last[-1]==' '||last[-1]=='-'||last[-1]=='\t'||last[-1]=='\n'))last--;
        size_t n=(size_t)(last-start);char token[80];if(n>=sizeof(token))return 0;memcpy(token,start,n);token[n]=0;
        if(!strncmp(token,"if ",3)){const char *condition=token+3;while(*condition==' ')condition++;const char *value=variable(condition,system,prompt,response);if(!value||depth>=16)return 0;stack[depth].parent=active;stack[depth].condition=*value!=0;stack[depth].otherwise=0;active=active&&*value;depth++;}
        else if(!strcmp(token,"else")){if(!depth||stack[depth-1].otherwise)return 0;stack[depth-1].otherwise=1;active=stack[depth-1].parent&&!stack[depth-1].condition;}
        else if(!strcmp(token,"end")){if(!depth)return 0;active=stack[--depth].parent;}
        else {const char *value=variable(token,system,prompt,response);if(!value)return 0;if(!strcmp(token,".Prompt"))*usesPrompt=1;if(active){if(final&&!strcmp(token,".Response"))return 1;if(!adds(out,value))return 0;}}
        p=end+2;
    }return !depth&&(!active||adds(out,p));
}
int legacy_template_supported(const char *format){Buffer out={0};int prompt=0;int ok=turn(&out,format,"system","user","assistant",0,&prompt)&&prompt;free(out.p);return ok;}
int legacy_prompt(Buffer *out,const char *format,const cJSON *messages,const char *system,char error[512]){
    if(!legacy_template_supported(format)){strcpy(error,"The model template needs the original Ollama runtime.");return 0;}
    const char *user=NULL;cJSON *m;int first=1,used=0;
    cJSON_ArrayForEach(m,messages){const char *role=str(m,"role");if(!strcmp(role,"system"))system=str(m,"content");else if(!strcmp(role,"user"))user=str(m,"content");else if(user){if(!turn(out,format,first?system:"",user,str(m,"content"),0,&used))goto failed;if(!strstr(format,".Response")&&!adds(out,str(m,"content")))goto failed;if(!adds(out,"\n"))goto failed;first=0;user=NULL;}}
    if(!user||!turn(out,format,first?system:"",user,"",1,&used))goto failed;return 1;
failed:strcpy(error,"Cannot render this model's conversation template within the prompt limit.");return 0;
}
