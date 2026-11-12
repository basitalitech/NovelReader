#pragma once

struct SceNetInitParam
{
    void *memory;
    int size;
    int flags;
};
extern "C" int sceNetInit(SceNetInitParam *param);
extern "C" int sceNetTerm();