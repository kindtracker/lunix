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

  const char *path = argv[1];
  int status = lunix_run(path, argc-1, argv+1);
  status=status; lunix_log("[lunix] exit status: %d\n", status);
  return 0;
}
