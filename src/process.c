#include <sys/socket.h>
#include <sys/wait.h>

#include <unicorn/unicorn.h>

#include "process.h"
#include "run.h"
#include "lunix.h"

lunix_process_manager_t *lunix_process_manager;

int lunix_next_pid = 1;

int lunix_process_manager_start_server() {
  lunix_process_manager = calloc(1, sizeof(*lunix_process_manager));
  if (!lunix_process_manager) {
    perror("[lunix] failed to allocate process manager");
    return 1;
  }

  lunix_process_manager->server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (lunix_process_manager->server_fd == -1) {
    perror("[lunix] failed to create a socket");
    return 1;
  }
  int server = lunix_process_manager->server_fd;

  unlink(LUNIX_SOCKET_PATH);

  struct sockaddr_un *server_addr;
  server_addr = &lunix_process_manager->server_addr;
  memset(server_addr, 0, sizeof(*server_addr));
  
  server_addr->sun_family = AF_UNIX;
  strcpy(server_addr->sun_path, LUNIX_SOCKET_PATH);

  if (bind(server, (struct sockaddr *)server_addr, sizeof(*server_addr)) == -1) {
    perror("[lunix] failed to bind server");
    return 1;
  }

  if (listen(server, LUNIX_MAX_PROCESSES) == -1) {
    perror("[lunix] failed to listen");
    return 1;
  }

  return 0;
};

int lunix_process_manager_create(uc_engine *puc, int ppid, uint64_t sp, int argc, char **argv) {
  pid_t pid = fork();
  if (pid == -1) {
    perror("[lunix] failed to fork");
    return -1;
  }

  int guest_pid = lunix_next_pid++;
  if (pid == 0) {
    int client_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (client_fd == -1) {
      perror("[lunix] failed to create socket");
      return 1;
    }

    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, LUNIX_SOCKET_PATH);
    if (connect(client_fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
      perror("[lunix] failed to connect");
      close(client_fd);
      return 1;
    }

    lunix_process_t *process = NULL;
    int status = lunix_run(argv[0], client_fd, guest_pid, puc, ppid, sp, argc, argv, &process);
    lunix_log("[lunix] process %d exited with status %d\n", process->pid, status);
    _exit(status);
  }

  return guest_pid;
}
