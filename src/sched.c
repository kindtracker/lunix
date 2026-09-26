#include <string.h>

#include "lunix.h"

bool LunixAlive = true;

int LunixScheduler() {
  if (LunixProcessCount == 0) {
    LunixAlive = false;
    return 1;
  }

  for (int i = 0; i < LunixProcessCount; i++) {
    LunixProcess *Process = LunixProcesses[i];
    switch (Process->State) {
    case LUNIX_PSTATE_WAITING:
      continue;

    case LUNIX_PSTATE_EXITED:
      uc_close(Process->UnicornVM);
      free(Process);

      memmove(&LunixProcesses[i], &LunixProcesses[i + 1],
              (LunixProcessCount - i - 1) * sizeof(LunixProcesses[0]));

      LunixProcessCount--;
      LunixProcesses[LunixProcessCount] = NULL;

      i--;
      continue;
    }

    uc_engine *UnicornVM = Process->UnicornVM;
    uint64_t ProgramCount;
    uc_reg_read(UnicornVM, UC_ARM64_REG_PC, &ProgramCount);

    uc_emu_start(UnicornVM, ProgramCount, 0, 0, 10000);
  }

  return 0;
}
