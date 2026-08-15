#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <elf.h>

#include <unicorn/unicorn.h>

#include "lunix.h"
#include "run.h"

int main(int argc, char **argv) {
  if (argc < 2) {
    printf("USAGE: lunix [PATH] [ARGS...]\n");
    return 0;
  }
  lunix_log("[lunix] v0.1.0\n");

  if (lunix_process_manager_start_server()) {
    fprintf(stderr, "[lunix] failed to start server\n");
    return -1;
  }

  lunix_process_manager_create(argc-1, argv+1);

  return 0;
}
