#pragma once

constexpr int SCE_NETCTL_STATE_CONNECTED = 3;
extern "C" int sceNetCtlInit();
extern "C" int sceNetCtlTerm();
extern "C" int sceNetCtlInetGetState(int* state);