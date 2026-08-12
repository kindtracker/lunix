static long syscall(long nr, long a0, long a1, long a2) {
  register long x8 asm("x8") = nr;
  register long x0 asm("x0") = a0;
  register long x1 asm("x1") = a1;
  register long x2 asm("x2") = a2;

  asm volatile(
    "svc #0"
    : "+r"(x0)
    : "r"(x1), "r"(x2), "r"(x8)
    : "memory"
  );

  return x0;
}

void _start(void) {
  static const char msg[] = "meow!\n";

  syscall(64, 1, (long)msg, sizeof(msg) - 1);
  syscall(93, 0, 0, 0);

  __builtin_unreachable();
}
