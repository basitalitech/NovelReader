#pragma once
#include "lprefix.h"
#include <limits.h>
// Newlib hides these C99 limits in strict C++ mode on Vita.
#ifndef LLONG_MAX
#define LLONG_MAX __LONG_LONG_MAX__
#define LLONG_MIN (-LLONG_MAX - 1LL)
#define ULLONG_MAX (LLONG_MAX * 2ULL + 1ULL)
#endif
#ifndef LUA_LIB
#define LUA_LIB
#endif
// Lua supports C++ exception unwinding. Preserve its C API while compiling its
// implementation as C++, so an allocation error also releases native HTML data.
extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}
