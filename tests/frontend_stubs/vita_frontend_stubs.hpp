#pragma once
#include <cstdint>
#include <sys/stat.h>
#include <dirent.h>
#include <map>
#include <string>
#include <cstdio>
#include <cerrno>
#include <mutex>
constexpr unsigned SCE_CTRL_LEFT=1,SCE_CTRL_RIGHT=2,SCE_CTRL_UP=4,SCE_CTRL_DOWN=8,
    SCE_CTRL_CROSS=16,SCE_CTRL_CIRCLE=32,SCE_CTRL_TRIANGLE=64,SCE_CTRL_SQUARE=128,
    SCE_CTRL_START=256,SCE_CTRL_SELECT=512,SCE_CTRL_LTRIGGER=1024,SCE_CTRL_RTRIGGER=2048;
using SceWChar16=char16_t;
struct SceIoStat { int st_mode=0; };
struct SceIoDirent { char d_name[256]{};SceIoStat d_stat; };
inline bool SCE_S_ISDIR(int mode){return S_ISDIR(mode);}
inline bool SCE_S_ISREG(int mode){return S_ISREG(mode);}
inline int sceIoGetstat(const char* path,SceIoStat* result){struct stat info{};const int error=lstat(path,&info);if(!error)result->st_mode=info.st_mode;return error;}
inline std::map<int,std::pair<DIR*,std::string>>& test_directories(){static std::map<int,std::pair<DIR*,std::string>> value;return value;}
inline std::mutex& test_directory_mutex(){static std::mutex value;return value;}
inline std::string& test_denied_directory(){static std::string value;return value;}
inline int sceIoDopen(const char* path){
    std::lock_guard<std::mutex> lock(test_directory_mutex());
    if(test_denied_directory()==path)return -EACCES;
    auto* directory=opendir(path);if(!directory)return -1;
    static int next=0;const int id=++next;test_directories()[id]={directory,path};return id;
}
inline int sceIoDread(int id,SceIoDirent* result){
    std::lock_guard<std::mutex> lock(test_directory_mutex());
    auto found=test_directories().find(id);if(found==test_directories().end())return -1;
    errno=0;const auto* entry=readdir(found->second.first);if(!entry)return errno?-1:0;
    *result={};std::snprintf(result->d_name,sizeof(result->d_name),"%s",entry->d_name);
    sceIoGetstat((found->second.second+'/'+entry->d_name).c_str(),&result->d_stat);return 1;
}
inline int sceIoDclose(int id){
    std::lock_guard<std::mutex> lock(test_directory_mutex());
    auto found=test_directories().find(id);if(found==test_directories().end())return -1;
    const int result=closedir(found->second.first);test_directories().erase(found);return result;
}
inline std::uint64_t& test_process_time(){static std::uint64_t value=1000000;return value;}
inline std::uint64_t sceKernelGetProcessTimeWide(){return test_process_time();}
constexpr int SCE_COMMON_DIALOG_STATUS_FINISHED=1,SCE_IME_DIALOG_BUTTON_ENTER=1,
    SCE_IME_LANGUAGE_ENGLISH=0,SCE_IME_TYPE_NUMBER=0,SCE_IME_TYPE_DEFAULT=0,
    SCE_IME_DIALOG_DIALOG_MODE_WITH_CANCEL=0;
struct SceImeDialogParam {
    int supportedLanguages=0,type=0,dialogMode=0,maxTextLength=0;
    const SceWChar16* title=nullptr;
    const SceWChar16* initialText=nullptr;
    SceWChar16* inputTextBuffer=nullptr;
};
struct SceImeDialogResult {int button=0;};
inline void sceImeDialogParamInit(SceImeDialogParam* p){*p={};}
inline int sceImeDialogInit(SceImeDialogParam*){return 0;}
inline int sceImeDialogAbort(){return 0;}
inline int sceImeDialogTerm(){return 0;}
inline int& test_ime_status(){static int value=0;return value;}
inline int& test_ime_button(){static int value=0;return value;}
inline int sceImeDialogGetStatus(){return test_ime_status();}
inline int sceImeDialogGetResult(SceImeDialogResult* result){result->button=test_ime_button();return 0;}
