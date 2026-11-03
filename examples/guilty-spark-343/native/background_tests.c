#define _CRT_SECURE_NO_WARNINGS
#define COBJMACROS
#include "spark.h"
#include <shobjidl.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){printf("FAIL background:%d: %s (%s)\n",__LINE__,#x,error);goto fail;}passed++;}while(0)
int background_tests(void){
    int passed=0;char error[512]={0};wchar_t temp[MAX_PATH],root[MAX_PATH],linkPath[MAX_PATH+64],exe[32768],data[MAX_PATH+64],key[100],other[100];
    OleInitialize(NULL);GetTempPathW(MAX_PATH,temp);GetTempFileNameW(temp,L"gsp",0,root);DeleteFileW(root);CreateDirectoryW(root,NULL);
    swprintf(linkPath,MAX_PATH+64,L"%ls\\343 startup.lnk",root);swprintf(data,MAX_PATH+64,L"%ls\\Workspace \x03a9",root);GetModuleFileNameW(NULL,exe,32768);
    CHECK(background_key(data,key));wchar_t withSlash[MAX_PATH+64];swprintf(withSlash,MAX_PATH+64,L"%ls\\",data);CharUpperBuffW(withSlash,(DWORD)wcslen(withSlash));CHECK(background_key(withSlash,other));CHECK(!wcscmp(key,other));CHECK(background_key(root,other));CHECK(wcscmp(key,other));
    CHECK(startup_status(linkPath,data,error)==0);CHECK(startup_set(linkPath,exe,data,1,error));CHECK(startup_status(linkPath,data,error)==1);
    IShellLinkW *link=NULL;IPersistFile *file=NULL;HRESULT result=CoCreateInstance(&CLSID_ShellLink,NULL,CLSCTX_INPROC_SERVER,&IID_IShellLinkW,(void**)&link);CHECK(SUCCEEDED(result));
    result=IShellLinkW_QueryInterface(link,&IID_IPersistFile,(void**)&file);CHECK(SUCCEEDED(result));CHECK(SUCCEEDED(IPersistFile_Load(file,linkPath,STGM_READ)));
    wchar_t target[32768],arguments[32768];CHECK(SUCCEEDED(IShellLinkW_GetPath(link,target,32768,NULL,SLGP_RAWPATH)));CHECK(!_wcsicmp(target,exe));CHECK(SUCCEEDED(IShellLinkW_GetArguments(link,arguments,32768)));
    wchar_t *full=allocate((wcslen(arguments)+8)*sizeof(wchar_t));swprintf(full,wcslen(arguments)+8,L"app %ls",arguments);int argc=0;wchar_t **argv=CommandLineToArgvW(full,&argc);free(full);CHECK(argc==4);CHECK(!wcscmp(argv[1],L"--background"));CHECK(!wcscmp(argv[2],L"--data-dir"));CHECK(!wcscmp(argv[3],data));LocalFree(argv);IPersistFile_Release(file);IShellLinkW_Release(link);
    CHECK(startup_set(linkPath,exe,data,1,error));CHECK(startup_status(linkPath,data,error)==1);CHECK(!startup_set(linkPath,exe,root,0,error));CHECK(startup_status(linkPath,data,error)==1);
    CHECK(startup_set(linkPath,exe,data,0,error));CHECK(startup_status(linkPath,data,error)==0);CHECK(startup_set(linkPath,exe,data,0,error));
    HANDLE foreign=CreateFileW(linkPath,GENERIC_WRITE,0,NULL,CREATE_NEW,0,NULL);DWORD written=0;WriteFile(foreign,"unrelated",9,&written,NULL);CloseHandle(foreign);
    CHECK(startup_status(linkPath,data,error)==-1);CHECK(!startup_set(linkPath,exe,data,1,error));CHECK(!startup_set(linkPath,exe,data,0,error));WIN32_FILE_ATTRIBUTE_DATA info;CHECK(GetFileAttributesExW(linkPath,GetFileExInfoStandard,&info));CHECK(info.nFileSizeLow==9);
    DeleteFileW(linkPath);RemoveDirectoryW(root);OleUninitialize();return passed;
fail:DeleteFileW(linkPath);RemoveDirectoryW(root);OleUninitialize();return -1;
}
