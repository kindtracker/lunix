#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <elf.h>

#include <unicorn/unicorn.h>

#include "lunix.h"

extern int load_elf(const char *path, uc_engine *uc, uint64_t *entry);

static int setup_stack(uc_engine *uc, int stack_top, int stack_size, int argc, const char **argv) {
  uc_err err = uc_mem_map(
      uc,
      stack_top - stack_size,
      stack_size,
      UC_PROT_READ | UC_PROT_WRITE
  );

  if (err != UC_ERR_OK) {
    fprintf(stderr, "[lunix] failed to map stack: %s\n", uc_strerror(err));
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
    err = uc_mem_write(uc, sp, argv[i], len);
    if (err != UC_ERR_OK) {
      free(arg_addrs);
      return -1;
    }
    arg_addrs[i] = sp;
  }

  sp &= ~0xFULL;

  uint64_t zero = 0;

  sp -= 8;

  err = uc_mem_write(uc, sp, &zero, 8);
  if (err != UC_ERR_OK) {
    free(arg_addrs);
    return -1;
  }

  sp -= 8;

  err = uc_mem_write(uc, sp, &zero, 8);
  if (err != UC_ERR_OK) {
    free(arg_addrs);
    return -1;
  }

  for (int i = argc - 1; i >= 0; i--) {
    sp -= 8;

    err = uc_mem_write(uc, sp, &arg_addrs[i], 8);
    if (err != UC_ERR_OK) {
      free(arg_addrs);
      return -1;
    }
  }

  free(arg_addrs);

  sp -= 8;

  uint64_t guest_argc = argc;

  err = uc_mem_write(uc, sp, &guest_argc, 8);
  if (err != UC_ERR_OK) {
    return -1;
  }

  err = uc_reg_write(uc, UC_ARM64_REG_SP, &sp);
  if (err != UC_ERR_OK) {
    return -1;
  }

  return 0;
}

static void hook_code(uc_engine *uc, uint64_t address, uint32_t size, void *user_data) {
  size=size; user_data=user_data;
  uint32_t insn;

  uc_mem_read(uc, address, &insn, sizeof(insn));

  // lunix_debug("insn: %x xor: %x\n", insn, insn & 0xffe0001f);
  if ((insn & 0xffe0001f) == 0xd4000001) {
    lunix_debug("[lunix] syscall\n");
    
    uint64_t result = lunix_syscall(uc);
    uc_reg_write(uc, UC_ARM64_REG_X0, &result);

    uint64_t pc = address + 4;
    uc_reg_write(uc, UC_ARM64_REG_PC, &pc);
  }
}

int main(int argc, const char **argv) {
  if (argc < 2) {
    printf("USAGE: lunix [PATH] [ARGS...]\n");
    return 0;
  }
  lunix_log("[lunix] v0.1.0\n");

  const char *path = argv[1];

  uint64_t entry;

  uc_engine *uc;
  uc_err err = uc_open(UC_ARCH_ARM64, UC_MODE_ARM, &uc);
  if (err != UC_ERR_OK) {
    fprintf(stderr, "[lunix] failed to init unicorn: %s\n", uc_strerror(err));
    return 1;
  }

  lunix_debug("[lunix] path: %s\n", path);
  if (load_elf(path, uc, &entry) != 0) {
    fprintf(stderr, "[lunix] failed to load program\n");
    return 1;
  }

  err = uc_mem_map(uc, LUNIX_HEAP_BASE, LUNIX_HEAP_SIZE, UC_PROT_READ | UC_PROT_WRITE);
  if (err != UC_ERR_OK) {
    fprintf(stderr, "[lunix] failed to map heap: %s\n", uc_strerror(err));
    return 1;
  }

  if (setup_stack(uc, 0x80000000, 0x10000, argc-1, argv+1) != 0) {
    fprintf(stderr, "[lunix] failed to setup stack\n");
    return 1;
  }
  uc_reg_write(uc, UC_ARM64_REG_PC, &entry);

  uc_hook hook;
  uc_hook_add(uc, &hook, UC_HOOK_CODE, (void *)hook_code, NULL, 1, 0);

  err = uc_emu_start(uc, entry, 0, 0, 0);
  if (err != UC_ERR_OK) {
    fprintf(stderr, "[lunix] emulation stopped: %s\n", uc_strerror(err));
    return 1;
  }

  return 0;
}
