#pragma once
#include <sys/socket.h>
#include <sys/un.h>

#include <unicorn/unicorn.h>

#define LUNIX_MAX_PROCESSES 64

typedef struct {
  int used;
  pid_t host_pid;
  int client_fd;

  int pid;
  int ppid;
  int uid;
  int gid;
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

typedef struct {
  uint32_t type;
  uint64_t r0;
  uint64_t r1;
  uint64_t r2;
  uint64_t r3;
  uint64_t r4;
  uint64_t r5;
  uint64_t r6;
  uint64_t r7;
  uint64_t data_len;
  char data[];
} lunix_message_t;

extern lunix_process_manager_t *lunix_process_manager;

extern int lunix_process_manager_start_server();
extern int lunix_process_manager_create(int ppid, int argc, char **argv);
