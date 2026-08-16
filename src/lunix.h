#pragma once
#include <unicorn/unicorn.h>

#include "process.h"

#define LUNIX_STACK_TOP 0x80000000
#define LUNIX_STACK_SIZE 0x10000
#define LUNIX_HEAP_BASE 0x50000000ULL
#define LUNIX_HEAP_SIZE 0x1000000ULL
#define LUNIX_MMAP_BASE 0x100000000ULL

#define LUNIX_DEFAULT_UID 1000
#define LUNIX_DEFAULT_GID 1000

#define LUNIX_SOCKET_PATH "/tmp/lunix.sock"

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

extern long lunix_syscall(uc_engine *uc, lunix_process_t *process);
