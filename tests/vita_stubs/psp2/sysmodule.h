#pragma once

constexpr unsigned short SCE_SYSMODULE_NET = 0;
extern "C" int sceSysmoduleLoadModule(unsigned short module);
extern "C" int sceSysmoduleUnloadModule(unsigned short module);
