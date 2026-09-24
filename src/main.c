#include <elf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unicorn/unicorn.h>

#include "lunix.h"

int main(int Argc, const char **Argv) {
  if (Argc < 2) {
    printf("USAGE: lunix [ProgramPath] [ARGS...]\n");
    return 0;
  }
  LunixLog("[Lunix] v0.1.0\n");

  const char *ProgramPath = Argv[1];
  LunixProcess *Process = LunixCreateProcess(ProgramPath, Argc, Argv);

  while (LunixAlive) {
    if (LunixScheduler() != 0) {
      break;
    }
  }

  free(Process);

  return 0;
}
