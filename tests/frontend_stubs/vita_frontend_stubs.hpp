#pragma once
#include <cstdint>
#include <sys/stat.h>
constexpr unsigned SCE_CTRL_LEFT=1,SCE_CTRL_RIGHT=2,SCE_CTRL_UP=4,SCE_CTRL_DOWN=8,
    SCE_CTRL_CROSS=16,SCE_CTRL_CIRCLE=32,SCE_CTRL_TRIANGLE=64,SCE_CTRL_SQUARE=128,
    SCE_CTRL_START=256,SCE_CTRL_SELECT=512,SCE_CTRL_LTRIGGER=1024,SCE_CTRL_RTRIGGER=2048;
using SceWChar16=char16_t;
struct SceIoStat { int st_mode=0; };
struct SceIoDirent { char d_name[256]{};SceIoStat d_stat; };
inline bool SCE_S_ISDIR(int mode){return S_ISDIR(mode);}
inline bool SCE_S_ISREG(int mode){return S_ISREG(mode);}
inline int sceIoGetstat(const char* path,SceIoStat* result){struct stat info{};const int error=stat(path,&info);if(!error)result->st_mode=info.st_mode;return error;}
inline int sceIoDopen(const char*){return 1;}
inline int sceIoDread(int,SceIoDirent*){return 0;}
inline int sceIoDclose(int){return 0;}
inline std::uint64_t sceKernelGetProcessTimeWide(){return 1000000;}
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
inline int sceImeDialogGetStatus(){return 0;}
inline int sceImeDialogGetResult(SceImeDialogResult*){return 0;}
