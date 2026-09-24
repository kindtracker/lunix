#include <elf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unicorn/unicorn.h>

#include "lunix.h"

extern int load_elf(const char *path, uc_engine *Unicorn, uint64_t *Entry);

static int setup_stack(uc_engine *Unicorn, int stack_top, int stack_size,
                       int argc, const char **argv) {
  uc_err Error = uc_mem_map(Unicorn, stack_top - stack_size, stack_size,
                            UC_PROT_READ | UC_PROT_WRITE);
  if (Error != UC_ERR_OK) {
    fprintf(stderr, "[Lunix] failed to map stack: %s\n", uc_strerror(Error));
    return -1;
  }

  uint64_t sp = stack_top - 16;
  sp &= ~0xFULL;

  uint64_t *arg_addrs = malloc(argc * sizeof(uint64_t));
  if (!arg_addrs) {
    return -1;
  }

  for (int i = argc - 1; i >= 0; i--) {
    size_t len = strlen(argv[i]) + 1;
    sp -= len;
    Error = uc_mem_write(Unicorn, sp, argv[i], len);
    if (Error != UC_ERR_OK) {
      free(arg_addrs);
      return -1;
    }
    arg_addrs[i] = sp;
  }

  sp &= ~0xFULL;

  uint64_t zero = 0;

  sp -= 8;

  Error = uc_mem_write(Unicorn, sp, &zero, 8);
  if (Error != UC_ERR_OK) {
    free(arg_addrs);
    return -1;
  }

  sp -= 8;

  Error = uc_mem_write(Unicorn, sp, &zero, 8);
  if (Error != UC_ERR_OK) {
    free(arg_addrs);
    return -1;
  }

  for (int i = argc - 1; i >= 0; i--) {
    sp -= 8;

    Error = uc_mem_write(Unicorn, sp, &arg_addrs[i], 8);
    if (Error != UC_ERR_OK) {
      free(arg_addrs);
      return -1;
    }
  }

  free(arg_addrs);

  sp -= 8;

  uint64_t guest_argc = argc;

  Error = uc_mem_write(Unicorn, sp, &guest_argc, 8);
  if (Error != UC_ERR_OK) {
    return -1;
  }

  Error = uc_reg_write(Unicorn, UC_ARM64_REG_SP, &sp);
  if (Error != UC_ERR_OK) {
    return -1;
  }

  return 0;
}

static void HookCode(uc_engine *Unicorn, uint64_t address, uint32_t size,
                     void *user_data) {
  size = size;
  user_data = user_data;
  uint32_t insn;

  uc_mem_read(Unicorn, address, &insn, sizeof(insn));

  // LunixDebug("insn: %x xor: %x\n", insn, insn & 0xffe0001f);
  if ((insn & 0xffe0001f) == 0xd4000001) {
    LunixDebug("[Lunix] syscall\n");

    uint64_t result = LunixSyscall(Unicorn);
    uc_reg_write(Unicorn, UC_ARM64_REG_X0, &result);

    uint64_t pc = address + 4;
    uc_reg_write(Unicorn, UC_ARM64_REG_PC, &pc);
  }
}

int main(int argc, const char **argv) {
  if (argc < 2) {
    printf("USAGE: lunix [PATH] [ARGS...]\n");
    return 0;
  }
  LunixLog("[Lunix] v0.1.0\n");

  const char *path = argv[1];

  uint64_t Entry;

  uc_engine *Unicorn;
  uc_err Error = uc_open(UC_ARCH_ARM64, UC_MODE_ARM, &Unicorn);
  if (Error != UC_ERR_OK) {
    fprintf(stderr, "[Lunix] failed to init unicorn: %s\n", uc_strerror(Error));
    return 1;
  }

  LunixDebug("[Lunix] path: %s\n", path);
  if (load_elf(path, Unicorn, &Entry) != 0) {
    fprintf(stderr, "[Lunix] failed to load program\n");
    return 1;
  }

  Error = uc_mem_map(Unicorn, LUNIX_HEAP_BASE, LUNIX_HEAP_SIZE,
                     UC_PROT_READ | UC_PROT_WRITE);
  if (Error != UC_ERR_OK) {
    fprintf(stderr, "[Lunix] failed to map heap: %s\n", uc_strerror(Error));
    return 1;
  }

  if (setup_stack(Unicorn, 0x80000000, 0x10000, argc - 1, argv + 1) != 0) {
    fprintf(stderr, "[Lunix] failed to setup stack\n");
    return 1;
  }
  uc_reg_write(Unicorn, UC_ARM64_REG_PC, &Entry);

  uc_hook hook;
  uc_hook_add(Unicorn, &hook, UC_HOOK_CODE, (void *)HookCode, NULL, 1, 0);

  Error = uc_emu_start(Unicorn, Entry, 0, 0, 0);
  if (Error != UC_ERR_OK) {
    fprintf(stderr, "[Lunix] emulation stopped: %s\n", uc_strerror(Error));
    return 1;
  }

  return 0;
}
