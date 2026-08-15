#pragma once
#include <sys/socket.h>
#include <sys/un.h>

#include <unicorn/unicorn.h>

#define LUNIX_MAX_PROCESSES 64

typedef struct {
  int used;
  int pid;
  int ppid;
  pid_t host_pid;
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

  int server_fd;

  struct sockaddr_un server_addr;
} lunix_process_manager_t;

extern lunix_process_manager_t *lunix_process_manager;

extern int lunix_process_manager_start_server();
extern int lunix_process_manager_create(int argc, char **argv);
