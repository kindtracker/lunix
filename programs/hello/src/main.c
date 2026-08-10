#include "lunix.h"

void _start(void) {
  const char msg[] = "helllo lunix!!!\n";

  LUNIX_SYSCALL(LUNIX_SYS_WRITE, 1, (long)msg, sizeof(msg) - 1);
  LUNIX_SYSCALL(LUNIX_SYS_EXIT, 0, 0, 0);

  for (;;);
}
