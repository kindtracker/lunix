#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <elf.h>

#include <unicorn/unicorn.h>

typedef struct {
  unsigned char *data;
  size_t size;
} file_t;

static file_t read_file(const char *path) {
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

int load_elf(const char *path, uc_engine *uc, uint64_t *entry) {
  file_t file = read_file(path);
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
