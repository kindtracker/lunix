#include <unistd.h>

#include <unicorn/unicorn.h>

#include "lunix.h"

static long lunix_sys_write(uc_engine *uc, int fd, uint64_t buf, unsigned long count) {
  char *data = malloc(count + 1);

  if (!data) {
    return -12;
  }

  uc_err err = uc_mem_read(uc, buf, data, count);
  if (err != UC_ERR_OK) {
    free(data);
    return -14;
  }

  data[count] = '\0';

  long result = write(fd, data, count);

  free(data);
  return result;
}

static long lunix_sys_exit(uc_engine *uc, int status) {
  uc=uc; lunix_log("[lunix] exit: %d", status);
  return 0;
}

long lunix_syscall(uc_engine *uc) {
  uint64_t number;
  uint64_t a0;
  uint64_t a1;
  uint64_t a2;

  uc_reg_read(uc, UC_ARM64_REG_X8, &number);
  uc_reg_read(uc, UC_ARM64_REG_X0, &a0);
  uc_reg_read(uc, UC_ARM64_REG_X1, &a1);
  uc_reg_read(uc, UC_ARM64_REG_X2, &a2);

  lunix_debug("[lunix] number: %lu\n", number);
  lunix_debug("[lunix] a0: %lu\n", a0);
  lunix_debug("[lunix] a1: %lu\n", a1);
  lunix_debug("[lunix] a2: %lu\n", a2);

  switch (number) {
    case 64:
      return lunix_sys_write(uc, (int)a0, a1, a2);

    case 93:
      return lunix_sys_exit(uc, (int)a0);

    default:
      return -1;
  }
}
