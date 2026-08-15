#pragma once
#include <unicorn/unicorn.h>

#define LUNIX_MAX_PROCESSES 64

typedef struct {
  int used;
  int pid;
  int ppid;
  uc_engine *uc;
  uint64_t entry;
  uint64_t stack;
  uint64_t brk;
  bool exited;
  int exit_status;
} lunix_process_t;

typedef struct {
  lunix_process_t processes[LUNIX_MAX_PROCESSES];
  int next_pid;
  int current_pid;
} lunix_process_manager_t;
