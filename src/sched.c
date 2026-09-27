#include <string.h>
#include <unistd.h>

#include "lunix.h"

bool LunixAlive = true;

int LunixScheduler() {
  if (LunixProcessCount == 0) {
    LunixAlive = false;
    return 1;
  }

  printf("%d\n", LunixProcessCount);
  for (int i = 0; i < LunixProcessCount; i++) {
    LunixProcess *Process = LunixProcesses[i];
    switch (Process->State) {
    case LUNIX_PSTATE_WAITING:
      break;

    case LUNIX_PSTATE_EXITED:
      uc_close(Process->UnicornVM);
      free(Process);

      memmove(&LunixProcesses[i], &LunixProcesses[i + 1],
              (LunixProcessCount - i - 1) * sizeof(LunixProcesses[0]));

      LunixProcessCount--;
      LunixProcesses[LunixProcessCount] = NULL;

      i--;
      break;
    }

    uc_engine *UnicornVM = Process->UnicornVM;
    uint64_t ProgramCount;
    uc_reg_read(UnicornVM, UC_ARM64_REG_PC, &ProgramCount);
    if (Process->LastWasSyscall) {
      ProgramCount += 4;
    }
    Process->LastWasSyscall = false;

    if (Process->State == LUNIX_PSTATE_READY) {
      printf("[DEBUG] Before emu_start - Process %d: PC=0x%lx, State=%d\n", i,
             ProgramCount, Process->State);
      uc_err err = uc_emu_start(UnicornVM, ProgramCount, 0, 0, 10000);
      printf("[DEBUG] After emu_start - err=%d, LastWasSyscall=%d\n", err,
             Process->LastWasSyscall);
      printf("Error string: %s\n", uc_strerror(err));
    }

    usleep(10 * 1000);
  }

  return 0;
}
