#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <elf.h>

#include <unicorn/unicorn.h>

#include "lunix.h"

typedef struct {
  unsigned char *data;
  size_t size;
} file_t;

static file_t lunix_read_file(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) {
    perror("fopen");
    exit(1);
  }

  fseek(f, 0, SEEK_END);
  size_t size = ftell(f);
  fseek(f, 0, SEEK_SET);

  unsigned char *data = malloc(size);
  if (!data || fread(data, 1, size, f) != size) {
    perror("fread");
    fclose(f);
    exit(1);
  }

  fclose(f);
  return (file_t){ .data = data, .size = size };
}

int lunix_load_elf(const char *path, uc_engine *uc, uint64_t *entry) {
  file_t file = lunix_read_file(path);
  if (file.size < sizeof(Elf64_Ehdr)) {
    free(file.data);
    return -1;
  }

  Elf64_Ehdr *eh = (Elf64_Ehdr *)file.data;
  if (memcmp(eh->e_ident, ELFMAG, SELFMAG) != 0 || eh->e_machine != EM_AARCH64) {
    free(file.data);
    return -1;
  }

  Elf64_Phdr *phdrs = (Elf64_Phdr *)(file.data + eh->e_phoff);

  uint64_t min_addr = UINT64_MAX;
  uint64_t max_addr = 0;
  for (int i = 0; i < eh->e_phnum; i++) {
    Elf64_Phdr *ph = &phdrs[i];
    if (ph->p_type != PT_LOAD) continue;

    uint64_t addr = ph->p_vaddr & ~0xfffULL;
    uint64_t end = (ph->p_vaddr + ph->p_memsz + 0xfff) & ~0xfffULL;
    if (min_addr > addr) min_addr = addr;
    if (max_addr < end) max_addr = end;
  }

  uc_err err = uc_mem_map(uc, min_addr, max_addr - min_addr, UC_PROT_ALL);
  if (err != UC_ERR_OK) {
    fprintf(stderr, "[lunix] failed to map elf segment: %s\n", uc_strerror(err));
    free(file.data);
    return -1;
  }

  for (int i = 0; i < eh->e_phnum; i++) {
    Elf64_Phdr *ph = &phdrs[i];
    if (ph->p_type != PT_LOAD) continue;
    
    uc_err err = uc_mem_write(uc, ph->p_vaddr, file.data + ph->p_offset, ph->p_filesz);
    if (err != UC_ERR_OK) {
      fprintf(stderr, "[lunix] failed to write elf segment: %s\n", uc_strerror(err));
      free(file.data);
      return -1;
    }

    if (ph->p_memsz > ph->p_filesz) {
      uint8_t *zero = calloc(1, ph->p_memsz - ph->p_filesz);
      if (!zero) {
        free(file.data);
        return -1;
      }

      err = uc_mem_write(uc, ph->p_vaddr + ph->p_filesz, zero, ph->p_memsz - ph->p_filesz);
      free(zero);

      if (err != UC_ERR_OK) {
        fprintf(stderr, "[lunix] failed to clear elf bss: %s\n", uc_strerror(err));
        free(file.data);
        return -1;
      }
    }
  }

  *entry = eh->e_entry;
  free(file.data);
  return 0;
}

int lunix_setup_stack(uc_engine *uc, uint64_t stack_top, uint64_t stack_size, uint64_t *stack_addr, int argc, char **argv) {
  uc_err err = uc_mem_map(uc, stack_top - stack_size, stack_size, UC_PROT_READ | UC_PROT_WRITE);
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

  *stack_addr = sp;
  err = uc_reg_write(uc, UC_ARM64_REG_SP, &sp);
  if (err != UC_ERR_OK) {
    return -1;
  }

  return 0;
}

void lunix_hook_code(uc_engine *uc, uint64_t address, uint32_t size, void *user_data) {
  size=size; user_data=user_data;
  uint32_t insn;

  uc_mem_read(uc, address, &insn, sizeof(insn));

  if ((insn & 0xffe0001f) == 0xd4000001) {
    lunix_debug("[lunix] syscall\n");
    
    uint64_t result = lunix_syscall(uc, (lunix_process_t *)user_data);
    uc_reg_write(uc, UC_ARM64_REG_X0, &result);

    uint64_t pc = address + 4;
    uc_reg_write(uc, UC_ARM64_REG_PC, &pc);
  }
}

int lunix_run(const char *path, int client_fd, int pid, uc_engine *puc, int ppid, uint64_t sp, int argc, char **argv, lunix_process_t **oprocess) {
  lunix_process_t *process = calloc(1, sizeof(lunix_process_t));
  if (!process) {
    perror("[lunix] failed to allocate process");
    return 1;
  }
  *oprocess = process;

  uc_engine *uc;
  uc_err err = uc_open(UC_ARCH_ARM64, UC_MODE_ARM, &uc);
  if (err != UC_ERR_OK) {
    fprintf(stderr, "[lunix] failed to init unicorn: %s\n", uc_strerror(err));
    return 1;
  }

  uint64_t entry = 0;
  if (!puc) {
    if (lunix_load_elf(path, uc, &entry) != 0) {
      fprintf(stderr, "[lunix] failed to load program\n");
      return 1;
    }

    err = uc_mem_map(uc, LUNIX_HEAP_BASE, LUNIX_HEAP_SIZE, UC_PROT_READ | UC_PROT_WRITE);
    if (err != UC_ERR_OK) {
      fprintf(stderr, "[lunix] failed to map heap: %s\n", uc_strerror(err));
      return 1;
    }
  }

  if (puc) {
    uc_mem_region *regions;
    uint32_t count;

    err = uc_mem_regions(puc, &regions, &count);
    if (err != UC_ERR_OK) {
      fprintf(stderr, "[lunix] failed to get memory regions: %s\n", uc_strerror(err));
      return -1;
    }

    for (uint32_t i = 0; i < count; i++) {
      uint64_t start = regions[i].begin;
      uint64_t size = regions[i].end - start + 1;
      uint8_t *buf = malloc(size);
      if (!buf) {
        uc_free(regions);
        return -1;
      }

      err = uc_mem_map(uc, start, size, regions[i].perms);
      if (err != UC_ERR_OK) {
        fprintf(stderr, "[lunix] failed to map memory: %s\n", uc_strerror(err));
        free(buf);
        uc_free(regions);
        return -1;
      }
      err = uc_mem_read(puc, start, buf, size);
      if (err == UC_ERR_OK) {
        err = uc_mem_write(uc, start, buf, size);
      }

      free(buf);
      if (err != UC_ERR_OK) {
        fprintf(stderr, "[lunix] failed to copy memory: %s\n", uc_strerror(err));
        uc_free(regions);
        return -1;
      }
    }
    uc_free(regions);

    err = uc_reg_read(puc, UC_ARM64_REG_PC, &entry);
    if (err != UC_ERR_OK) {
      fprintf(stderr, "[lunix] failed to get parent pc: %s\n", uc_strerror(err));
      return -1;
    }
    uint64_t regs[31];
    for (int i = 0; i < 31; i++) {
      uc_reg_read(puc, UC_ARM64_REG_X0 + i, &regs[i]);
    }
    for (int i = 0; i < 31; i++) {
      uc_reg_write(uc, UC_ARM64_REG_X0 + i, &regs[i]);
    }

    uint64_t zero = 0;
    uc_reg_write(uc, UC_ARM64_REG_X0, &zero);
  }

  uint64_t stack_addr;
  uint64_t real_sp = sp == 1 ? LUNIX_STACK_TOP : sp;
  if (!puc) {
    if (lunix_setup_stack(uc, real_sp, LUNIX_STACK_SIZE, &stack_addr, argc, argv) != 0) {
      fprintf(stderr, "[lunix] failed to setup stack\n");
      return 1;
    }
  } else {
    uc_reg_write(uc, UC_ARM64_REG_SP, &real_sp);
  }

  uc_reg_write(uc, UC_ARM64_REG_PC, &entry);

  uc_hook hook;
  uc_hook_add(uc, &hook, UC_HOOK_CODE, (void *)lunix_hook_code, process, 1, 0);

  process->uc = uc;
  process->host_pid = getpid();
  process->client_fd = client_fd;
  process->argc = argc;
  process->argv = argv;
  process->pid = pid;
  process->ppid = ppid;
  process->uid = LUNIX_DEFAULT_UID;
  process->gid = LUNIX_DEFAULT_GID;
  process->sp = real_sp;
  process->exited = false;
  process->exit_status = 0;

  err = uc_emu_start(uc, entry, 0, 0, 0);
  if (err != UC_ERR_OK) {
    fprintf(stderr, "[lunix] emulation stopped: %s\n", uc_strerror(err));
    return 1;
  }

  return process->exit_status;
}
