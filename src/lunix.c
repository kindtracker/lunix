#include <sys/random.h>
#include <unistd.h>

#include <unicorn/unicorn.h>

#include "lunix.h"

static uint64_t heap_end = LUNIX_HEAP_BASE;

// 64
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

// 93
static long lunix_sys_exit(uc_engine *uc, int status) {
  uc=uc; status=status; lunix_log("[lunix] exit: %d\n", status);
  uc_emu_stop(uc);
  return 0;
}

// 96
static long lunix_sys_set_tid_address(uc_engine *uc, uint64_t tidptr) {
  uc=uc;tidptr=tidptr;
  return 0;
}

// 99
static long lunix_sys_set_robust_list(uc_engine *uc, uint64_t head, uint64_t len) {
  uc=uc;head=head;len=len;
  return 0;
}

// 214
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

// 261
static long lunix_sys_prlimit64(uc_engine *uc, uint64_t pid, uint64_t resource, uint64_t new_limit, uint64_t old_limit) {
  uc=uc;pid=pid;resource=resource;new_limit=new_limit;old_limit=old_limit;
  return 0;
}

// 278
static long lunix_sys_getrandom(uc_engine *uc, uint64_t buf, uint64_t len, uint64_t flags) {
  uint8_t *data = malloc(len);
  if (!data) {
    return -12;
  }
  
  ssize_t result = getrandom(data, len, (unsigned int)flags);
  if (result < 0) {
    free(data);
    return -1;
  }
  
  uc_err err = uc_mem_write(uc, buf, data, result);
  if (err != UC_ERR_OK) {
    return -14;
  }

  free(data);
  return result;
}

// 293
static long lunix_sys_rseq(uc_engine *uc, uint64_t rseq, uint64_t rseq_len, uint64_t flags, uint64_t sig) {
  uc=uc;rseq=rseq;rseq_len=rseq_len;flags=flags;sig=sig;
  return 0;
}

long lunix_syscall(uc_engine *uc) {
  uint64_t number;
  uint64_t r0;
  uint64_t r1;
  uint64_t r2;
  uint64_t r3;

  uc_reg_read(uc, UC_ARM64_REG_X8, &number);
  uc_reg_read(uc, UC_ARM64_REG_X0, &r0);
  uc_reg_read(uc, UC_ARM64_REG_X1, &r1);
  uc_reg_read(uc, UC_ARM64_REG_X2, &r2);
  uc_reg_read(uc, UC_ARM64_REG_X3, &r3);

  lunix_debug("[lunix] number: %lu\n", number);
  lunix_debug("[lunix] r0: %lu\n", r0);
  lunix_debug("[lunix] r1: %lu\n", r1);
  lunix_debug("[lunix] r2: %lu\n", r2);
  lunix_debug("[lunix] r3: %lu\n", r3);

  switch (number) {
    case 64:
      return lunix_sys_write(uc, (int)r0, r1, r2);

    case 93:
      return lunix_sys_exit(uc, (int)r0);

    case 96:
      return lunix_sys_set_tid_address(uc, r0);

    case 99:
      return lunix_sys_set_robust_list(uc, r0, r1);

    case 214:
      return lunix_sys_brk(uc, r0);

    case 261:
      return lunix_sys_prlimit64(uc, r0, r1, r2, r3);
    
    case 278:
      return lunix_sys_getrandom(uc, r0, r1, r2);

    case 293:
      return lunix_sys_rseq(uc, r0, r1, r2, r3);

    default:
      lunix_log("[lunix] unimplemented syscall: %lu\n", number);
      return -38;
  }
}
