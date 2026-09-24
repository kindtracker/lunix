#pragma once
#include <unicorn/unicorn.h>

#define LUNIX_HEAP_BASE 0x50000000ULL
#define LUNIX_HEAP_SIZE 0x1000000ULL
#define LUNIX_MMAP_BASE 0x100000000ULL

#define LUNIX_ENABLE_LOG
// #define LUNIX_ENABLE_DEBUG

#ifdef LUNIX_ENABLE_LOG
#define LunixLog printf
#else
#define LunixLog(...) ((void)0)
#endif

#ifdef LUNIX_ENABLE_DEBUG
#define LunixDebug printf
#else
#define LunixDebug(...) ((void)0)
#endif

extern long LunixSyscall(uc_engine *Unicorn);
