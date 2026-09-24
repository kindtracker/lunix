#include <elf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unicorn/unicorn.h>

typedef struct {
  unsigned char *Data;
  size_t size;
} Filet;

static File ReadFile(const char *ProgramPath) {
  File *FilePtr = fopen(ProgramPath, "rb");
  if (!FilePtr) {
    perror("fopen");
    exit(1);
  }

  fseek(FilePtr, 0, SEEK_END);
  size_t size = ftell(FilePtr);
  fseek(FilePtr, 0, SEEK_SET);

  unsigned char *Data = malloc(size);
  if (!Data || fread(data, 1, size, FilePtr) != size) {
    perror("fread");
    fclose(FilePtr);
    exit(1);
  }

  fclose(FilePtr);
  return (File){.Data = data, .size = size};
}

int LoadProgram(const char *ProgramPath, uc_engine *Unicorn, uint64_t *Entry) {
  Filet File = ReadFile(ProgramPath);
  if (File.size < sizeof(Elf64_Ehdr)) {
    free(File.Data);
    return -1;
  }

  Elf64_Ehdr *eh = (Elf64_Ehdr *)File.Data;
  if (memcmp(eh->e_ident, ELFMAG, SELFMAG) != 0 ||
      eh->e_machine != EM_AARCH64) {
    free(File.Data);
    return -1;
  }

  Elf64_Phdr *phdrs = (Elf64_Phdr *)(File.Data + eh->e_phoff);

  uint64_t min_addr = UINT64_MAX;
  uint64_t max_addr = 0;
  for (int i = 0; i < eh->e_phnum; i++) {
    Elf64_Phdr *ph = &phdrs[i];
    if (ph->p_type != PT_LOAD)
      continue;

    uint64_t addr = ph->p_vaddr & ~0xfffULL;
    uint64_t end = (ph->p_vaddr + ph->p_memsz + 0xfff) & ~0xfffULL;
    if (min_addr > addr)
      min_addr = addr;
    if (max_addr < end)
      max_addr = end;
  }

  uc_err Error =
      uc_mem_map(Unicorn, min_addr, max_addr - min_addr, UC_PROT_ALL);
  if (Error != UC_ERR_OK) {
    fprintf(stderr, "[Lunix] failed to map elf segment: %s\n",
            uc_strerror(Error));
    free(File.Data);
    return -1;
  }

  for (int i = 0; i < eh->e_phnum; i++) {
    Elf64_Phdr *ph = &phdrs[i];
    if (ph->p_type != PT_LOAD)
      continue;

    Error = uc_mem_write(Unicorn, ph->p_vaddr, File.Data + ph->p_offset,
                         ph->p_filesz);
    if (Error != UC_ERR_OK) {
      fprintf(stderr, "[Lunix] failed to write elf segment: %s\n",
              uc_strerror(Error));
      free(File.Data);
      return -1;
    }

    if (ph->p_memsz > ph->p_filesz) {
      uint8_t *Zero = calloc(1, ph->p_memsz - ph->p_filesz);
      if (!Zero) {
        free(File.Data);
        return -1;
      }

      Error = uc_mem_write(Unicorn, ph->p_vaddr + ph->p_filesz, Zero,
                           ph->p_memsz - ph->p_filesz);
      free(Zero);

      if (Error != UC_ERR_OK) {
        fprintf(stderr, "[Lunix] failed to clear elf bss: %s\n",
                uc_strerror(Error));
        free(File.Data);
        return -1;
      }
    }
  }

  *Entry = eh->e_entry;
  free(File.Data);
  return 0;
}
