#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/random.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/syscall.h>

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <fcntl.h>
#include <time.h>
#include <unistd.h>

#include <unicorn/unicorn.h>

#include "lunix.h"

typedef uint32_t Fd_t;

static uint64_t HeapEnd = LUNIX_HEAP_BASE;

struct linux_arm64_stat {
  uint64_t st_dev;
  uint64_t st_ino;
  uint32_t st_mode;
  uint32_t st_nlink;
  uint32_t st_uid;
  uint32_t st_gid;
  uint64_t st_rdev;
  uint64_t __pad1;
  int64_t st_size;
  int32_t st_blksize;
  int32_t __pad2;
  int64_t st_blocks;
  int64_t st_atime_sec;
  int64_t st_atime_nsec;
  int64_t st_mtime_sec;
  int64_t st_mtime_nsec;
  int64_t st_ctime_sec;
  int64_t st_ctime_nsec;
  uint32_t __unused4;
  uint32_t __unused5;
};

struct linux_dirent64 {
  uint64_t d_ino;
  int64_t d_off;
  unsigned short d_RecLength;
  unsigned char d_type;
  char d_name[];
};

struct linux_utsname {
  char sysname[65];
  char nodename[65];
  char release[65];
  char version[65];
  char machine[65];
};

static int32_t LunixTranslateOpenFlags(int32_t GuestFlags) {
  int32_t HostFlags = 0;
  switch (GuestFlags & 3) {
  case 0:
    HostFlags |= O_RDONLY;
    break;
  case 1:
    HostFlags |= O_WRONLY;
    break;
  case 2:
    HostFlags |= O_RDWR;
    break;
  default:
    return -1;
  }

  if (GuestFlags & 0x40)
    HostFlags |= O_CREAT;
  if (GuestFlags & 0x80)
    HostFlags |= O_EXCL;
  if (GuestFlags & 0x200)
    HostFlags |= O_TRUNC;
  if (GuestFlags & 0x400)
    HostFlags |= O_APPEND;
  if (GuestFlags & 0x800)
    HostFlags |= O_NONBLOCK;
  if (GuestFlags & 0x10000)
    HostFlags |= O_DIRECTORY;
  if (GuestFlags & 0x20000)
    HostFlags |= O_NOFOLLOW;
  if (GuestFlags & 0x80000)
    HostFlags |= O_CLOEXEC;
  return HostFlags;
}

static long LunixReadString(uc_engine *Unicorn, uint64_t Address, char *Out,
                            uint64_t OutSize) {
  for (uint64_t i = 0; i < OutSize - 1; i++) {
    uint8_t c;
    uc_err Error = uc_mem_read(Unicorn, Address + i, &c, 1);
    if (Error != UC_ERR_OK) {
      return -EFAULT;
    }
    Out[i] = (char)c;
    if (c == '\0') {
      return 0;
    }
  }

  Out[OutSize - 1] = '\0';
  return -ENAMETOOLONG;
}

// 17
static long LunixSyscallGetcwd(uc_engine *Unicorn, uint64_t BufferAddress,
                               uint64_t size) {
  char Buffer[4096];
  if (getcwd(Buffer, sizeof(Buffer)) == NULL) {
    return -errno;
  }

  uint64_t Length = strlen(Buffer);
  if (Length > size) {
    return -ERANGE;
  }

  uc_err Error = uc_mem_write(Unicorn, BufferAddress, &Buffer, Length);
  if (Error != UC_ERR_OK) {
    return -EFAULT;
  }
  return BufferAddress;
}

// 25
static long LunixSyscallFcntl(uc_engine *Unicorn, uint32_t Fd, uint32_t cmd,
                              uint64_t arg) {
  struct flock flock;
  int32_t Result;
  uc_err Error;

  switch (cmd) {
  case F_DUPFD:
  case F_DUPFD_CLOEXEC:
  case F_SETFD:
  case F_SETFL:
    Result = fcntl(Fd, cmd, arg);
    break;

  case F_GETFD:
  case F_GETFL:
    Result = fcntl(Fd, cmd);
    break;

  case F_GETLK:
  case F_SETLK:
  case F_SETLKW:
    Error = uc_mem_read(Unicorn, arg, &flock, sizeof(flock));
    if (Error != UC_ERR_OK) {
      return -EFAULT;
    }

    Result = fcntl(Fd, cmd, &flock);

    if (Result == -1) {
      return -errno;
    }

    Error = uc_mem_write(Unicorn, arg, &flock, sizeof(flock));
    if (Error != UC_ERR_OK) {
      return -EFAULT;
    }

    return Result;

  default:
    return -ENOSYS;
  }

  if (Result == -1)
    return -errno;

  return Result;
}

// 29
static long LunixSyscallIoctl(uc_engine *Unicorn, uint64_t Fd, uint64_t cmd,
                              uint64_t arg) {
  Unicorn = Unicorn;
  int32_t Result = ioctl(Fd, cmd, arg);
  return Result;
}

// 56
static long LunixSyscallOpenat(uc_engine *Unicorn, int32_t DirFd,
                               uint64_t PathNameAddress, int32_t Flags,
                               uint32_t mode) {
  char PathName[4096];
  long Result =
      LunixReadString(Unicorn, PathNameAddress, PathName, sizeof(PathName));
  if (Result < 0) {
    return Result;
  }
  PathName[sizeof(PathName) - 1] = '\0';

  Flags = LunixTranslateOpenFlags(Flags);
  int32_t Fd = openat(DirFd, PathName, Flags, mode);
  if (Fd < 0) {
    return -errno;
  }
  return Fd;
}

// 57
static long LunixSyscallClose(uc_engine *Unicorn, int32_t Fd) {
  Unicorn = Unicorn;
  if (close(Fd) < 0) {
    return -errno;
  }
  return 0;
}

// 61
static long LunixSyscallGetdents64(uc_engine *Unicorn, int32_t Fd,
                                   uint64_t dirp, uint32_t Count) {
  char *Buffer = malloc(Count);
  if (!Buffer) {
    return -ENOMEM;
  }

  long Result = syscall(SYS_getdents64, Fd, Buffer, Count);

  if (Result < 0) {
    Result = -errno;
    free(Buffer);
    return Result;
  }

  uint64_t pos = 0;
  uint64_t written = 0;

  while (pos < (uint64_t)Result) {
    struct linux_dirent64 *host_entry = (struct linux_dirent64 *)(Buffer + pos);

    uint64_t NameLength = strlen(host_entry->d_name);

    uint64_t RecLength =
        offsetof(struct linux_dirent64, d_name) + NameLength + 1;

    RecLength = (RecLength + 7) & ~7UL;

    if (written + RecLength > Count) {
      break;
    }

    struct linux_dirent64 guest_entry = {.d_ino = host_entry->d_ino,
                                         .d_off = host_entry->d_off,
                                         .d_RecLength = RecLength,
                                         .d_type = host_entry->d_type};

    uc_err Error = uc_mem_write(Unicorn, dirp + written, &guest_entry,
                                offsetof(struct linux_dirent64, d_name));

    if (Error != UC_ERR_OK) {
      free(Buffer);
      return -EFAULT;
    }

    Error = uc_mem_write(
        Unicorn, dirp + written + offsetof(struct linux_dirent64, d_name),
        host_entry->d_name, NameLength + 1);

    if (Error != UC_ERR_OK) {
      free(Buffer);
      return -EFAULT;
    }

    written += RecLength;
    pos += host_entry->d_RecLength;
  }

  free(Buffer);
  return written;
}

// 62
static long LunixSyscalllSeek(uc_engine *Unicorn, uint32_t Fd, int64_t Offset,
                              uint32_t whence) {
  int64_t Result = lseek(Fd, Offset, whence);

  if (Result == -1) {
    return -errno;
  }

  return Result;
}

// 63
static long LunixSyscallRead(uc_engine *Unicorn, int32_t Fd,
                             uint64_t BufferAddress, uint64_t Count) {
  char *Buffer = malloc(Count);
  uint64_t Result = read(Fd, Buffer, Count);
  if (!Result) {
    free(Buffer);
    return -ENOMEM;
  }

  uc_err Error = uc_mem_write(Unicorn, BufferAddress, Buffer, Count);
  free(Buffer);
  if (Error != UC_ERR_OK) {
    return -EFAULT;
  }
  return Result;
}

// 64
static long LunixSyscallWrite(uc_engine *Unicorn, int32_t Fd, uint64_t Buffer,
                              uint64_t Count) {
  char *Data = malloc(Count + 1);
  if (!Data) {
    return -12;
  }

  uc_err Error = uc_mem_read(Unicorn, Buffer, Data, Count);
  if (Error != UC_ERR_OK) {
    free(Data);
    return -14;
  }
  Data[Count] = '\0';

  long Result = write(Fd, Data, Count);
  free(Data);
  return Result;
}

// 71
static long LunixSyscallSendfile(uc_engine *Unicorn, int32_t OutFd,
                                 int32_t InFd, uint64_t OffsetAddress,
                                 uint64_t Count) {
  int64_t Offset;
  if (OffsetAddress != 0) {
    uc_err Error = uc_mem_read(Unicorn, OffsetAddress, &Offset, sizeof(Offset));
    if (Error != UC_ERR_OK) {
      return -EFAULT;
    }
  }

  uint64_t Result =
      sendfile(OutFd, InFd, OffsetAddress ? &Offset : NULL, Count);
  if (Result < 0) {
    return -errno;
  }

  if (OffsetAddress != 0) {
    uc_err Error =
        uc_mem_write(Unicorn, OffsetAddress, &Offset, sizeof(Offset));
    if (Error != UC_ERR_OK) {
      return -EFAULT;
    }
  }
  return Result;
}

// 78
static long LunixSyscallReadlinkat(uc_engine *Unicorn, int32_t DirFd,
                                   uint64_t Pathname, uint64_t Buffer,
                                   uint64_t Length) {
  if (Length == 0) {
    return 0;
  }

  char PathName[4096];
  long sResult = LunixReadString(Unicorn, Pathname, PathName, sizeof(PathName));
  if (sResult < 0) {
    return sResult;
  }
  PathName[sizeof(PathName) - 1] = '\0';
  char *Data = malloc(Length);
  if (!Data) {
    return -12;
  }

  uint64_t Result = readlinkat(DirFd, PathName, Data, Length);
  if (Result >= 0) {
    uc_err Error = uc_mem_write(Unicorn, Buffer, Data, Result);
    if (Error != UC_ERR_OK) {
      Result = -14;
    }
  }
  free(Data);
  return Result;
}

// 79
static long LunixSyscallNewfstat(uc_engine *Unicorn, int32_t DirFd,
                                 uint64_t PathNameAddress, uint64_t StatAddress,
                                 int32_t Flags) {
  char PathName[4096];
  long Result =
      LunixReadString(Unicorn, PathNameAddress, PathName, sizeof(PathName));
  if (Result < 0) {
    return Result;
  }
  PathName[sizeof(PathName) - 1] = '\0';

  struct stat host;
  if (fstatat(DirFd, PathName, &host, Flags) < 0) {
    return -errno;
  }

  struct linux_arm64_stat guest = {
      .st_dev = host.st_dev,
      .st_ino = host.st_ino,
      .st_mode = host.st_mode,
      .st_nlink = host.st_nlink,
      .st_uid = host.st_uid,
      .st_gid = host.st_gid,
      .st_rdev = host.st_rdev,
      .st_size = host.st_size,
      .st_blksize = host.st_blksize,
      .st_blocks = host.st_blocks,
      .st_atime_sec = host.st_atim.tv_sec,
      .st_atime_nsec = host.st_atim.tv_nsec,
      .st_mtime_sec = host.st_mtim.tv_sec,
      .st_mtime_nsec = host.st_mtim.tv_nsec,
      .st_ctime_sec = host.st_ctim.tv_sec,
      .st_ctime_nsec = host.st_ctim.tv_nsec,
  };

  uc_err Error = uc_mem_write(Unicorn, StatAddress, &guest, sizeof(guest));
  if (Error != UC_ERR_OK) {
    return -14;
  }
  return 0;
}

// 80
static long LunixSyscallFstat(uc_engine *Unicorn, int32_t Fd,
                              uint64_t StatAddress) {
  struct stat host;
  if (fstat(Fd, &host) < 0) {
    return -errno;
  }

  struct linux_arm64_stat guest = {
      .st_dev = host.st_dev,
      .st_ino = host.st_ino,
      .st_mode = host.st_mode,
      .st_nlink = host.st_nlink,
      .st_uid = host.st_uid,
      .st_gid = host.st_gid,
      .st_rdev = host.st_rdev,
      .st_size = host.st_size,
      .st_blksize = host.st_blksize,
      .st_blocks = host.st_blocks,
      .st_atime_sec = host.st_atim.tv_sec,
      .st_atime_nsec = host.st_atim.tv_nsec,
      .st_mtime_sec = host.st_mtim.tv_sec,
      .st_mtime_nsec = host.st_mtim.tv_nsec,
      .st_ctime_sec = host.st_ctim.tv_sec,
      .st_ctime_nsec = host.st_ctim.tv_nsec,
  };

  uc_err Error = uc_mem_write(Unicorn, StatAddress, &guest, sizeof(guest));
  if (Error != UC_ERR_OK) {
    return -14;
  }
  return 0;
}

// 93
static long LunixSyscallExit(uc_engine *Unicorn, LunixProcess *Process,
                             int32_t Status) {
  Unicorn = Unicorn;
  Status = Status;
  LunixLog("[lunix] exit: %d\n", Status);
  LunixRemoveProcess(Process);
  return 0;
}

// 93
static long LunixSyscallExit_group(uc_engine *Unicorn, LunixProcess *Process,
                                   int32_t Status) {
  Status = Status;
  LunixRemoveProcess(Process);
  return 0;
}

// 96
static long LunixSyscallSet_tid_addr(uc_engine *Unicorn, uint64_t tidptr) {
  Unicorn = Unicorn;
  tidptr = tidptr;
  return 0;
}

// 99
static long LunixSyscallSet_robust_list(uc_engine *Unicorn, uint64_t head,
                                        uint64_t Length) {
  Unicorn = Unicorn;
  head = head;
  Length = Length;
  return 0;
}

// 113
static long LunixSyscallClock_Gettime(uc_engine *Unicorn, int32_t which_clock,
                                      uint64_t tp) {
  struct timespec ts;
  if (clock_gettime(which_clock, &ts) < 0) {
    return -errno;
  }

  uc_err Error = uc_mem_write(Unicorn, tp, &ts, sizeof(ts));
  if (Error != UC_ERR_OK) {
    return -14;
  }
  return 0;
}

// 134
static long LunixSyscallRt_sigaction(uc_engine *Unicorn, int32_t sig,
                                     uint64_t act, uint64_t oldact,
                                     uint64_t sigsetsize) {
  Unicorn = Unicorn;
  sig = sig;
  act = act;
  oldact = oldact;
  sigsetsize = sigsetsize;
  return 0;
}

// 135
static long LunixSyscallRt_sigprocmask(uc_engine *Unicorn, int32_t how,
                                       uint64_t set, uint64_t oset,
                                       uint64_t sigsetsize) {
  Unicorn = Unicorn;
  how = how;
  set = set;
  oset = oset;
  sigsetsize = sigsetsize;
  return 0;
}

// 144
static long LunixSyscallSetgid(uc_engine *Unicorn, uint64_t gid) {
  Unicorn = Unicorn;
  gid = gid;
  return 0;
}

// 145
static long LunixSyscallSetregid(uc_engine *Unicorn, uint64_t egid) {
  Unicorn = Unicorn;
  egid = egid;
  return 0;
}

// 146
static long LunixSyscallSetuid(uc_engine *Unicorn, uint64_t uid) {
  Unicorn = Unicorn;
  uid = uid;
  return 0;
}

// 147
static long LunixSyscallSetreuid(uc_engine *Unicorn, uint64_t euid) {
  Unicorn = Unicorn;
  euid = euid;
  return 0;
}

// 160
static long LunixSyscallUname(uc_engine *Unicorn, uint64_t Address) {
  struct linux_utsname uts = {0};
  strcpy(uts.sysname, "Linux");
  strcpy(uts.nodename, "lunix");
  strcpy(uts.release, "6.16.7");
  strcpy(uts.version, "#1 lunix");
  strcpy(uts.machine, "aarch64");

  uc_err Error = uc_mem_write(Unicorn, Address, &uts, sizeof(uts));
  if (Error != UC_ERR_OK) {
    return -EFAULT;
  }
  return 0;
}

// 172
static long LunixSyscallGetpid(uc_engine *Unicorn) {
  Unicorn = Unicorn;
  return getpid();
}

// 173
static long LunixSyscallGetppid(uc_engine *Unicorn) {
  Unicorn = Unicorn;
  return getppid();
}

// 174
static long LunixSyscallGetuid(uc_engine *Unicorn) {
  Unicorn = Unicorn;
  return getuid();
}

// 175
static long LunixSyscallGeteuid(uc_engine *Unicorn) {
  Unicorn = Unicorn;
  return getuid();
}

// 176
static long LunixSyscallGetgid(uc_engine *Unicorn) {
  Unicorn = Unicorn;
  return getgid();
}

// 177
static long LunixSyscallGetegid(uc_engine *Unicorn) {
  Unicorn = Unicorn;
  return getgid();
}

// 214
static long LunixSyscallBrk(uc_engine *Unicorn, uint64_t addr) {
  Unicorn = Unicorn;
  if (addr == 0) {
    return HeapEnd;
  }
  if (addr < LUNIX_HEAP_BASE || addr > LUNIX_HEAP_BASE + LUNIX_HEAP_SIZE) {
    return HeapEnd;
  }
  HeapEnd = addr;
  return HeapEnd;
}

// 220
static long LunixSyscallClone(uc_engine *Unicorn, LunixProcess *Process,
                              uint64_t Flags, uint64_t StackTop,
                              uint64_t ParentTid, uint64_t ChildTid,
                              uint64_t Tls) {
  StackTop = StackTop & ~0xFFFULL;

  printf("0x%x 0x%x 0x%x 0x%x\n", Flags, StackTop, ParentTid, ChildTid, Tls);
  LunixProcess *NewProcess =
      LunixCreateBlankProcess(StackTop, Process->StackSize);

  if (NewProcess == NULL) {
    return -1;
  }
  return 1;
}

// 221
static long LunixSyscallExecve(uc_engine *Unicorn, LunixProcess *Process,
                               uint64_t GProgramPath, uint64_t GArgv,
                               uint64_t GEnvp) {
  char Argv[64][4096];
  char *ArgvPointers[64];

  uint64_t ArgvPointer;
  uint64_t Index = 0;

  while (Index < 63) {
    if (uc_mem_read(Unicorn, GArgv + Index * sizeof(uint64_t), &ArgvPointer,
                    sizeof(ArgvPointer)) != UC_ERR_OK)
      return -1;

    if (ArgvPointer == 0)
      break;

    if (LunixReadString(Unicorn, ArgvPointer, Argv[Index],
                        sizeof(Argv[Index])) < 0)
      return -1;

    ArgvPointers[Index] = Argv[Index];
    Index++;
  }
  ArgvPointers[Index] = NULL;

  char ProgramPath[4096];
  LunixReadString(Unicorn, GProgramPath, ProgramPath, sizeof(ProgramPath));

  //  TODO: replace process instead of creating new one
  LunixCreateProcess(ProgramPath, Index, (const char **)ArgvPointers);
  LunixRemoveProcess(Process);
}

// 222
static long LunixSyscallMmap(uc_engine *Unicorn, uint64_t Address,
                             uint64_t Length, int32_t Protect, int32_t Flags,
                             int32_t Fd, int64_t Offset) {
  Fd = Fd;
  Offset = Offset;
  static uint64_t mmap_next = LUNIX_MMAP_BASE;
  if (Length == 0)
    return -EINVAL;

  uint64_t size = (Length + 0xfffULL) & ~0xfffULL;
  uint64_t start = Address & ~0xfffULL;

  if (Address == 0)
    start = mmap_next;

  uint64_t end = start + size;
  if (end <= start)
    return -EINVAL;

  int32_t Perms = 0;
  if (Protect & PROT_READ)
    Perms |= UC_PROT_READ;
  if (Protect & PROT_WRITE)
    Perms |= UC_PROT_WRITE;
  if (Protect & PROT_EXEC)
    Perms |= UC_PROT_EXEC;

  uc_err Error = uc_mem_map(Unicorn, start, size, Perms);
  if (Error != UC_ERR_OK) {
    LunixLog("[lunix] mmap: start=0x%lx size=0x%lx Protect=0x%x Flags=0x%x\n",
             start, size, Protect, Flags);
    LunixLog("[lunix] mmap: %s\n", uc_strerror(Error));
    return -ENOMEM;
  }

  if (Address == 0)
    mmap_next = end;

  return start;
}

// 226
static long LunixSyscallMProtectect(uc_engine *Unicorn, uint64_t Address,
                                    uint64_t Length, int32_t Protect) {
  uint64_t start = Address & ~0xfffULL;
  uint64_t end = (Address + Length + 0xfff) & ~0xfffULL;
  if (end <= start) {
    return -22;
  }

  int32_t Perms = 0;
  if (Protect & PROT_READ)
    Perms |= UC_PROT_READ;
  if (Protect & PROT_WRITE)
    Perms |= UC_PROT_WRITE;
  if (Protect & PROT_EXEC)
    Perms |= UC_PROT_EXEC;

  uc_err Error = uc_mem_protect(Unicorn, start, end - start, Perms);
  if (Error != UC_ERR_OK) {
    return -22;
  }

  return 0;
}

// 233
static long LunixSyscallMadvise(uc_engine *Unicorn, uint64_t Address,
                                uint64_t Length, int32_t advice) {
  Unicorn = Unicorn;
  Address = Address;
  Length = Length;
  advice = advice;
  return 0;
}

// 240
static long LunixSyscallWait4(uc_engine *Unicorn, LunixProcess *Process,
                              int32_t ProcessId, uint64_t Wstatus,
                              int32_t Options, uint64_t Rusage) {
  Process->State = LUNIX_PSTATE_WAITING;
  Process->WaitingForPid = ProcessId;
  return 0;
}

// 261
static long LunixSyscallPrlimit64(uc_engine *Unicorn, uint64_t pid,
                                  uint64_t resource, uint64_t new_limit,
                                  uint64_t old_limit) {
  Unicorn = Unicorn;
  pid = pid;
  resource = resource;
  new_limit = new_limit;
  old_limit = old_limit;
  return 0;
}

// 278
static long LunixSyscallGetrandom(uc_engine *Unicorn, uint64_t Buffer,
                                  uint64_t Length, uint32_t Flags) {
  uint8_t *Data = malloc(Length);
  if (!Data) {
    return -12;
  }

  uint64_t Result = getrandom(Data, Length, Flags);
  if (Result < 0) {
    free(Data);
    return -1;
  }

  uc_err Error = uc_mem_write(Unicorn, Buffer, Data, Result);
  if (Error != UC_ERR_OK) {
    return -14;
  }

  free(Data);
  return Result;
}

// 293
static long LunixSyscallRseq(uc_engine *Unicorn, uint64_t rseq,
                             uint64_t RseqLength, uint64_t Flags,
                             uint64_t sig) {
  Unicorn = Unicorn;
  rseq = rseq;
  RseqLength = RseqLength;
  Flags = Flags;
  sig = sig;
  return 0;
}

long LunixSyscall(LunixProcess *Process) {
  uc_engine *Unicorn = Process->UnicornVM;
  uint64_t SyscallNumber;
  uint64_t Reg0;
  uint64_t Reg1;
  uint64_t Reg2;
  uint64_t Reg3;

  uc_reg_read(Unicorn, UC_ARM64_REG_X8, &SyscallNumber);
  uc_reg_read(Unicorn, UC_ARM64_REG_X0, &Reg0);
  uc_reg_read(Unicorn, UC_ARM64_REG_X1, &Reg1);
  uc_reg_read(Unicorn, UC_ARM64_REG_X2, &Reg2);
  uc_reg_read(Unicorn, UC_ARM64_REG_X3, &Reg3);

  LunixDebug("[lunix] SyscallNumber: %lu\n", SyscallNumber);
  LunixDebug("[lunix] Reg0: %lu\n", Reg0);
  LunixDebug("[lunix] Reg1: %lu\n", Reg1);
  LunixDebug("[lunix] Reg2: %lu\n", Reg2);
  LunixDebug("[lunix] Reg3: %lu\n", r3);

  switch (SyscallNumber) {
  case 17:
    return LunixSyscallGetcwd(Unicorn, Reg0, Reg1);

  case 25:
    return LunixSyscallFcntl(Unicorn, Reg0, Reg1, Reg2);

  case 29:
    return LunixSyscallIoctl(Unicorn, Reg0, Reg1, Reg2);

  case 56:
    return LunixSyscallOpenat(Unicorn, Reg0, Reg1, Reg2, Reg3);

  case 57:
    return LunixSyscallClose(Unicorn, Reg0);

  case 61:
    return LunixSyscallGetdents64(Unicorn, Reg0, Reg1, Reg2);

  case 62:
    return LunixSyscallRead(Unicorn, Reg0, Reg1, Reg2);

  case 63:
    return LunixSyscallRead(Unicorn, Reg0, Reg1, Reg2);

  case 64:
    return LunixSyscallWrite(Unicorn, Reg0, Reg1, Reg2);

  case 71:
    return LunixSyscallSendfile(Unicorn, Reg0, Reg1, Reg2, Reg3);

  case 78:
    return LunixSyscallReadlinkat(Unicorn, Reg0, Reg1, Reg2, Reg3);

  case 79:
    return LunixSyscallNewfstat(Unicorn, Reg0, Reg1, Reg2, Reg3);

  case 80:
    return LunixSyscallFstat(Unicorn, Reg0, Reg1);

  case 93:
    return LunixSyscallExit(Unicorn, Process, Reg0);

  case 94:
    return LunixSyscallExit_group(Unicorn, Process, Reg0);

  case 96:
    return LunixSyscallSet_tid_addr(Unicorn, Reg0);

  case 99:
    return LunixSyscallSet_robust_list(Unicorn, Reg0, Reg1);

  case 113:
    return LunixSyscallClock_Gettime(Unicorn, Reg0, Reg1);

  case 134:
    return LunixSyscallRt_sigaction(Unicorn, Reg0, Reg1, Reg2, Reg3);

  case 135:
    return LunixSyscallRt_sigprocmask(Unicorn, Reg0, Reg1, Reg2, Reg3);

  case 144:
    return LunixSyscallSetgid(Unicorn, Reg0);

  case 145:
    return LunixSyscallSetregid(Unicorn, Reg0);

  case 146:
    return LunixSyscallSetuid(Unicorn, Reg0);

  case 147:
    return LunixSyscallSetreuid(Unicorn, Reg0);

  case 160:
    return LunixSyscallUname(Unicorn, Reg0);

  case 172:
    return LunixSyscallGetpid(Unicorn);

  case 173:
    return LunixSyscallGetppid(Unicorn);

  case 174:
    return LunixSyscallGetuid(Unicorn);

  case 175:
    return LunixSyscallGeteuid(Unicorn);

  case 176:
    return LunixSyscallGetgid(Unicorn);

  case 177:
    return LunixSyscallGetegid(Unicorn);

  case 214:
    return LunixSyscallBrk(Unicorn, Reg0);

  case 220: {
    uint64_t Reg4;
    uc_reg_read(Unicorn, UC_ARM64_REG_X4, &Reg4);
    LunixDebug("[Lunix] Reg4: %lu\n", Reg4);
    return LunixSyscallClone(Unicorn, Process, Reg0, Reg1, Reg2, Reg3, Reg4);
  }

  case 221:
    return LunixSyscallExecve(Unicorn, Process, Reg0, Reg1, Reg2);

  case 222: {
    uint64_t Reg4;
    uint64_t Reg5;
    uc_reg_read(Unicorn, UC_ARM64_REG_X4, &Reg4);
    uc_reg_read(Unicorn, UC_ARM64_REG_X5, &Reg5);
    LunixDebug("[Lunix] Reg4: %lu\n", Reg4);
    LunixDebug("[Lunix] Reg5: %lu\n", Reg5);
    return LunixSyscallMmap(Unicorn, Reg0, Reg1, Reg2, Reg3, Reg4, Reg5);
  }

  case 261:
    return LunixSyscallPrlimit64(Unicorn, Reg0, Reg1, Reg2, Reg3);

  case 226:
    return LunixSyscallMProtectect(Unicorn, Reg0, Reg1, Reg2);

  case 233:
    return LunixSyscallMadvise(Unicorn, Reg0, Reg1, Reg2);

  case 260:
    return LunixSyscallWait4(Unicorn, Process, Reg0, Reg1, Reg2, Reg3);

  case 278:
    return LunixSyscallGetrandom(Unicorn, Reg0, Reg1, Reg2);

  case 293:
    return LunixSyscallRseq(Unicorn, Reg0, Reg1, Reg2, Reg3);

  default:
    LunixLog("[Lunix] unimplemented syscall: %lu\n", SyscallNumber);
    return -38;
  }
}
