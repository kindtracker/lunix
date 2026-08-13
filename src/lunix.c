#include <unistd.h>

#include <unicorn/unicorn.h>

#include "lunix.h"

static uint64_t heap_end = LUNIX_HEAP_BASE;

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
  uc=uc; status=status; lunix_log("[lunix] exit: %d\n", status);
  uc_emu_stop(uc);
  return 0;
}

static long lunix_sys_brk(uc_engine *uc, uint64_t address) {
  uc=uc;
  if (address == 0) {
    return heap_end;
  }
  if (address < LUNIX_HEAP_BASE || address > LUNIX_HEAP_BASE + LUNIX_HEAP_SIZE) {
    return heap_end;
  }
  heap_end = address;
  return heap_end;
}

long lunix_syscall(uc_engine *uc) {
  uint64_t number;
  uint64_t r0;
  uint64_t r1;
  uint64_t r2;

  uc_reg_read(uc, UC_ARM64_REG_X8, &number);
  uc_reg_read(uc, UC_ARM64_REG_X0, &r0);
  uc_reg_read(uc, UC_ARM64_REG_X1, &r1);
  uc_reg_read(uc, UC_ARM64_REG_X2, &r2);

  lunix_debug("[lunix] number: %lu\n", number);
  lunix_debug("[lunix] r0: %lu\n", r0);
  lunix_debug("[lunix] r1: %lu\n", r1);
  lunix_debug("[lunix] r2: %lu\n", r2);

  switch (number) {
    case 64:
      return lunix_sys_write(uc, (int)r0, r1, r2);

    case 93:
      return lunix_sys_exit(uc, (int)r0);

    case 214:
      return lunix_sys_brk(uc, r0);

    default:
      lunix_log("[lunix] unimplemented syscall: %lu\n", number);
      return -38;
  }
}
