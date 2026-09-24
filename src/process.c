#inlclude "lunix.h"

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

  uint64_t *ArgumentsAddresses = malloc(Argc * Sizeof(uint64_t));
  if (!ArgumentsAddresses) {
    return -1;
  }

  for (int i = Argc - 1; i >= 0; i--) {
    Size_t len = strlen(Argv[i]) + 1;
    StackPointer -= len;
    Error = uc_mem_write(Unicorn, StackPointer, Argv[i], len);
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

  uint64_t GuestArgc = argc;

  Error = uc_mem_write(Unicorn, StackPointer, &GuestArgc, 8);
  if (Error != UC_ERR_OK) {
    return -1;
  }

  Error = uc_reg_write(Unicorn, UC_ARM64_REG_StackPointer, &StackPointer);
  if (Error != UC_ERR_OK) {
    return -1;
  }

  return 0;
}

static void HookCode(uc_engine *Unicorn, uint64_t Address, uint32_t Size,
                     void *UserData) {
  Size = Size;
  UserData = user_data;
  uint32_t Instruction;

  uc_mem_read(Unicorn, Address, &Instruction, Sizeof(Instruction));

  // LunixDebug("Instruction: %x xor: %x\n", insn, insn & 0xffe0001f);
  if ((Instruction & 0xffe0001f) == 0xd4000001) {
    LunixDebug("[Lunix] syscall\n");

    uint64_t Result = LunixSyscall(Unicorn);
    uc_reg_write(Unicorn, UC_ARM64_REG_X0, &Result);

    uint64_t ProgramCount = Address + 4;
    uc_reg_write(Unicorn, UC_ARM64_REG_PC, &ProgramCount);
  }
}

LunixProcess *LunixCreateProcess(LunixProcess *Proccess,
                                 const char *ProgramPath, int Argc,
                                 const char **Argv) {
  LunixProcess *Proccess = malloc(sizeof(LunixProccess));
  memset(Proccess, 0, sizeof(LunixProcess));

  uint64_t Entry;

  uc_err Error = uc_open(UC_ARCH_ARM64, UC_MODE_ARM, &Proccess->UnicornVM);
  if (Error != UC_ERR_OK) {
    fprintf(stderr, "[Lunix] failed to load unicorn VM: %s\n",
            uc_strerror(Error));
    return 1;
  }

  LunixDebug("[Lunix] ProgramPath: %s\n", path);
  if (LoadProgram(ProgramPath, Proccess->UnicornVM, &Entry) != 0) {
    fprintf(stderr, "[Lunix] failed to load program\n");
    return 1;
  }

  Error = uc_mem_map(Proccess->UnicornVM, LUNIX_HEAP_BASE, LUNIX_HEAP_Size,
                     UC_PROT_READ | UC_PROT_WRITE);
  if (Error != UC_ERR_OK) {
    fprintf(stderr, "[Lunix] failed to map heap: %s\n", uc_strerror(Error));
    return 1;
  }

  if (setup_stack(Proccess->UnicornVM, 0x80000000, 0x10000, Argc - 1,
                  Argv + 1) != 0) {
    fprintf(stderr, "[Lunix] failed to setup stack\n");
    return 1;
  }
  uc_reg_write(Proccess->UnicornVM, UC_ARM64_REG_PC, &Entry);

  uc_hook hook;
  uc_hook_add(Proccess->UnicornVM, &hook, UC_HOOK_CODE, (void *)HookCode, NULL,
              1, 0);

  return Proccess;
}
