#include <string.h>

#include "lunix.h"

int SetupStack(uc_engine *Unicorn, int StackTop, int StackSize, int Argc,
               const char **Argv) {
  uc_err Error = uc_mem_map(Unicorn, StackTop - StackSize, StackSize,
                            UC_PROT_READ | UC_PROT_WRITE);
  if (Error != UC_ERR_OK) {
    fprintf(stderr, "[Lunix] failed to map stack: %s\n", uc_strerror(Error));
    return -1;
  }

  uint64_t StackPointer = StackTop - 16;
  StackPointer &= ~0xFULL;

  uint64_t *ArgumentsAddresses = malloc(Argc * sizeof(uint64_t));
  if (!ArgumentsAddresses) {
    return -1;
  }

  for (int i = Argc - 1; i >= 0; i--) {
    size_t Length = strlen(Argv[i]) + 1;
    StackPointer -= Length;
    Error = uc_mem_write(Unicorn, StackPointer, Argv[i], Length);
    if (Error != UC_ERR_OK) {
      free(ArgumentsAddresses);
      return -1;
    }
    ArgumentsAddresses[i] = StackPointer;
  }

  StackPointer &= ~0xFULL;

  uint64_t Zero = 0;

  StackPointer -= 8;

  Error = uc_mem_write(Unicorn, StackPointer, &Zero, 8);
  if (Error != UC_ERR_OK) {
    free(ArgumentsAddresses);
    return -1;
  }

  StackPointer -= 8;

  Error = uc_mem_write(Unicorn, StackPointer, &Zero, 8);
  if (Error != UC_ERR_OK) {
    free(ArgumentsAddresses);
    return -1;
  }

  for (int i = Argc - 1; i >= 0; i--) {
    StackPointer -= 8;

    Error = uc_mem_write(Unicorn, StackPointer, &ArgumentsAddresses[i], 8);
    if (Error != UC_ERR_OK) {
      free(ArgumentsAddresses);
      return -1;
    }
  }

  free(ArgumentsAddresses);

  StackPointer -= 8;

  uint64_t GuestArgc = Argc;

  Error = uc_mem_write(Unicorn, StackPointer, &GuestArgc, 8);
  if (Error != UC_ERR_OK) {
    return -1;
  }

  Error = uc_reg_write(Unicorn, UC_ARM64_REG_SP, &StackPointer);
  if (Error != UC_ERR_OK) {
    return -1;
  }

  return 0;
}

static void HookCode(uc_engine *Unicorn, uint64_t Address, uint32_t Size,
                     void *UserData) {
  Size = Size;
  UserData = UserData;
  uint32_t Instruction;

  uc_mem_read(Unicorn, Address, &Instruction, sizeof(Instruction));

  // LunixDebug("Instruction: %x xor: %x\n", insn, insn & 0xffe0001f);
  if ((Instruction & 0xffe0001f) == 0xd4000001) {
    LunixDebug("[Lunix] syscall\n");

    LunixProcess *Process = (LunixProcess *)UserData;
    uint64_t Result = LunixSyscall(Process);
    uc_reg_write(Unicorn, UC_ARM64_REG_X0, &Result);

    uint64_t ProgramCount = Address + 4;
    uc_reg_write(Unicorn, UC_ARM64_REG_PC, &ProgramCount);
  }
}

LunixProcess *LunixCreateProcess(const char *ProgramPath, int Argc,
                                 const char **Argv) {
  LunixProcess *Process = malloc(sizeof(LunixProcess));
  memset(Process, 0, sizeof(LunixProcess));

  uint64_t Entry;

  uc_err Error = uc_open(UC_ARCH_ARM64, UC_MODE_ARM, &Process->UnicornVM);
  if (Error != UC_ERR_OK) {
    fprintf(stderr, "[Lunix] failed to load unicorn VM: %s\n",
            uc_strerror(Error));
    return NULL;
  }

  LunixDebug("[Lunix] ProgramPath: %s\n", path);
  if (LunixLoadProgram(ProgramPath, Process->UnicornVM, &Entry) != 0) {
    fprintf(stderr, "[Lunix] failed to load program\n");
    return NULL;
  }

  Error = uc_mem_map(Process->UnicornVM, LUNIX_HEAP_BASE, LUNIX_HEAP_SIZE,
                     UC_PROT_READ | UC_PROT_WRITE);
  if (Error != UC_ERR_OK) {
    fprintf(stderr, "[Lunix] failed to map heap: %s\n", uc_strerror(Error));
    return NULL;
  }

  if (SetupStack(Process->UnicornVM, 0x80000000, 0x10000, Argc - 1, Argv + 1) !=
      0) {
    fprintf(stderr, "[Lunix] failed to initalize stack\n");
    return NULL;
  }
  uc_reg_write(Process->UnicornVM, UC_ARM64_REG_PC, &Entry);

  uc_hook Hook;
  uc_hook_add(Process->UnicornVM, &Hook, UC_HOOK_CODE, (void *)HookCode,
              Process, 1, 0);

  return Process;
}
