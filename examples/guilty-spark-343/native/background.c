#define _CRT_SECURE_NO_WARNINGS
#define COBJMACROS
#include "spark.h"
#include <shlobj.h>
#include <shobjidl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const wchar_t *startup_description=L"Guilty Spark 343 background importer";
static wchar_t *startup_arguments(const wchar_t *directory){const wchar_t *argv[]={L"--background",L"--data-dir",directory,NULL};return argv_command_line(argv);}
int background_key(const wchar_t *directory,wchar_t out[100]){
    wchar_t *absolute=allocate(32768*sizeof(wchar_t));DWORD n=GetFullPathNameW(directory,32768,absolute,NULL);
    if(!n||n>=32768){free(absolute);return 0;}while(n>3&&(absolute[n-1]==L'\\'||absolute[n-1]==L'/'))absolute[--n]=0;CharLowerBuffW(absolute,n);
    char *text=utf8(absolute),hash[65];free(absolute);int ok=text&&spark_digest("GuiltySpark343 workspace:",text,hash);free(text);if(!ok)return 0;
    wchar_t *wideHash=wide(hash);swprintf(out,100,L"Local\\GuiltySpark343-%ls",wideHash);free(wideHash);return 1;
}
int startup_path(wchar_t out[32768],char error[512]){
    PWSTR folder=NULL;HRESULT result=SHGetKnownFolderPath(&FOLDERID_Startup,KF_FLAG_CREATE,NULL,&folder);
    if(FAILED(result)||!folder){strcpy(error,"Cannot locate this Windows account's Startup folder.");return 0;}
    int ok=wcslen(folder)+32<32768;if(ok)swprintf(out,32768,L"%ls\\Guilty Spark 343.lnk",folder);else strcpy(error,"The Windows Startup path is too long.");CoTaskMemFree(folder);return ok;
}
// Tests pass a link path in an isolated directory, never the account Startup folder.
int startup_status(const wchar_t *linkPath,const wchar_t *directory,char error[512]){
    DWORD attributes=GetFileAttributesW(linkPath);if(attributes==INVALID_FILE_ATTRIBUTES){DWORD code=GetLastError();if(code==ERROR_FILE_NOT_FOUND||code==ERROR_PATH_NOT_FOUND)return 0;strcpy(error,"Cannot inspect the 343 startup shortcut.");return -1;}
    if(attributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)){strcpy(error,"The 343 startup path is occupied by another item; it was preserved.");return -1;}
    IShellLinkW *link=NULL;IPersistFile *file=NULL;int status=-1;wchar_t *arguments=allocate(32768*sizeof(wchar_t)),description[256];wchar_t *expected=startup_arguments(directory);
    HRESULT result=CoCreateInstance(&CLSID_ShellLink,NULL,CLSCTX_INPROC_SERVER,&IID_IShellLinkW,(void**)&link);
    if(SUCCEEDED(result))result=IShellLinkW_QueryInterface(link,&IID_IPersistFile,(void**)&file);
    if(SUCCEEDED(result))result=IPersistFile_Load(file,linkPath,STGM_READ);
    if(SUCCEEDED(result))result=IShellLinkW_GetDescription(link,description,256);
    if(SUCCEEDED(result))result=IShellLinkW_GetArguments(link,arguments,32768);
    if(SUCCEEDED(result)&&!wcscmp(description,startup_description)&&expected&&!wcscmp(arguments,expected))status=1;
    if(status<0)strcpy(error,"A different or unreadable startup shortcut uses the 343 name; it was preserved.");
    free(expected);free(arguments);if(file)IPersistFile_Release(file);if(link)IShellLinkW_Release(link);return status;
}
int startup_set(const wchar_t *linkPath,const wchar_t *executable,const wchar_t *directory,int enabled,char error[512]){
    error[0]=0;int existing=startup_status(linkPath,directory,error);if(existing<0)return 0;
    if(!enabled){if(!existing||DeleteFileW(linkPath))return 1;strcpy(error,"Cannot remove the 343 startup shortcut.");return 0;}
    if(GetFileAttributesW(executable)==INVALID_FILE_ATTRIBUTES){strcpy(error,"The 343 executable is unavailable; startup was not changed.");return 0;}
    IShellLinkW *link=NULL;IPersistFile *file=NULL;wchar_t *arguments=startup_arguments(directory),*working=_wcsdup(executable),*temporary=allocate(32768*sizeof(wchar_t));
    wchar_t *slash=wcsrchr(working,L'\\');if(slash)*slash=0;char id[64];newid(id);wchar_t *uuid=wide(id);int valid=wcslen(linkPath)+wcslen(uuid)+6<32768;if(valid)swprintf(temporary,32768,L"%ls.%ls.tmp",linkPath,uuid);free(uuid);
    HRESULT result=valid?CoCreateInstance(&CLSID_ShellLink,NULL,CLSCTX_INPROC_SERVER,&IID_IShellLinkW,(void**)&link):E_INVALIDARG;
    if(SUCCEEDED(result))result=IShellLinkW_SetPath(link,executable);
    if(SUCCEEDED(result))result=IShellLinkW_SetArguments(link,arguments);
    if(SUCCEEDED(result))result=IShellLinkW_SetWorkingDirectory(link,working);
    if(SUCCEEDED(result))result=IShellLinkW_SetDescription(link,startup_description);
    if(SUCCEEDED(result))result=IShellLinkW_SetShowCmd(link,SW_SHOWNORMAL);
    if(SUCCEEDED(result))result=IShellLinkW_SetIconLocation(link,executable,0);
    if(SUCCEEDED(result))result=IShellLinkW_QueryInterface(link,&IID_IPersistFile,(void**)&file);
    if(SUCCEEDED(result))result=IPersistFile_Save(file,temporary,TRUE);
    if(file)IPersistFile_Release(file);if(link)IShellLinkW_Release(link);
    int ok=SUCCEEDED(result)&&MoveFileExW(temporary,linkPath,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
    if(!ok){if(valid)DeleteFileW(temporary);strcpy(error,"Cannot save the Windows startup shortcut; the previous entry was preserved.");}
    free(arguments);free(working);free(temporary);return ok;
}
