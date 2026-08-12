#pragma once
#include <unicorn/unicorn.h>

#define LUNIX_ENABLE_LOG
// #define LUNIX_ENABLE_DEBUG

#ifdef LUNIX_ENABLE_LOG
#define lunix_log printf
#else
#define lunix_log(...) ((void)0)
#endif

#ifdef LUNIX_ENABLE_DEBUG
#define lunix_debug printf
#else
#define lunix_debug(...) ((void)0)
#endif

extern long lunix_syscall(uc_engine *uc);
