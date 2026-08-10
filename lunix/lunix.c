#include <unistd.h>
#include "lunix.h"

static long lunix_sys_write(int fd, const void *buf, unsigned long count) {
    return write(fd, buf, count);
}

static long lunix_sys_exit(int status) {
    _exit(status);
    return 0;
}

long lunix_syscall(long number, long a0, long a1, long a2) {
  switch (number) {
    case LUNIX_SYS_WRITE:
      return lunix_sys_write((int)a0, (const void *)a1, (unsigned long)a2);

    case LUNIX_SYS_EXIT:
      return lunix_sys_exit((int)a0);

    default:
      return -1;
  }
}
