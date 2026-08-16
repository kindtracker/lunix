#pragma once
#include <unicorn/unicorn.h>
#include "process.h"

extern int lunix_load_elf(const char *path, uc_engine *uc, uint64_t *entry);
extern int lunix_setup_stack(uc_engine *uc, uint64_t stack_top, uint64_t stack_size, int argc, const char **argv);
extern void lunix_hook_code(uc_engine *uc, uint64_t address, uint32_t size, void *user_data);
extern int lunix_run(const char *path, int client_fd, int pid, uc_engine *uc, int ppid, uint64_t sp, int argc, char **argv, lunix_process_t **oprocess);
