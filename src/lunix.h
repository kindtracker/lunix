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

typedef enum { LUNIX_PSTATE_READY, LUNIX_PSTATE_WAITING } LunixProcessState;

typedef struct {
  uc_engine *UnicornVM;
  uint32_t ProccessId;
  uint64_t StackTop;
  uint64_t StackSize;

  LunixProcessState State;

  int WaitingForPid;
} LunixProcess;

extern int LunixActiveProcessCount;
extern int LunixProcessCount;
extern LunixProcess *LunixProcesses[64];
extern bool LunixAlive;

int LunixLoadProgram(const char *ProgramPath, uc_engine *Unicorn,
                     uint64_t *Entry);
long LunixSyscall(LunixProcess *Process);

LunixProcess *LunixCreateProcess(const char *ProgramPath, int Argc,
                                 const char **Argv);
LunixProcess *LunixCreateBlankProcess(uint64_t StackTop, uint64_t StackSize);
int LunixRemoveProcess(LunixProcess *Process);
int LunixScheduler();
