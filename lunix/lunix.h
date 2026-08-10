#pragma once

enum {
  LUNIX_SYS_EXIT = 0,
  LUNIX_SYS_WRITE = 1,
  LUNIX_SYS_READ = 2,
  LUNIX_SYS_OPEN = 3,
  LUNIX_SYS_CLOSE = 4,
};

typedef long (*lunix_syscall_fn)(long, long, long, long);
#define LUNIX_SYSCALL ((lunix_syscall_fn)0x700000000000)

extern long lunix_syscall(long number, long a0, long a1, long a2);
