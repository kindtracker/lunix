#include <sys/socket.h>
#include <sys/wait.h>

#include <unicorn/unicorn.h>

#include "process.h"
#include "run.h"
#include "lunix.h"

lunix_process_manager_t *lunix_process_manager;

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

int lunix_process_manager_create(int argc, char **argv) {
  pid_t pid = fork();
  if (pid == -1) {
    perror("[lunix] failed to fork");
    return -1;
  }
  if (pid == 0) {
    int status = lunix_run(argv[0], argc, argv);
    _exit(status);
  }

  int status;
  waitpid(pid, &status, 0);
  if (WIFEXITED(status)) {
    lunix_log("[lunix] exit status: %d\n", WEXITSTATUS(status));
  }

  return status;
}
