#include <sys/sendfile.h>
#include <sys/syscall.h> 
#include <sys/random.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>

#include <unistd.h>
#include <time.h>
#include <fcntl.h>

#include <unicorn/unicorn.h>

#include "lunix.h"
#include "process.h"

typedef uint32_t fd_t;

static uint64_t heap_end = LUNIX_HEAP_BASE;

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
  int64_t  d_off;
  unsigned short d_reclen;
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

static int lunix_translate_open_flags(int guest_flags) {
  int host_flags = 0;
  switch (guest_flags & 3) {
    case 0:
      host_flags |= O_RDONLY;
      break;
    case 1:
      host_flags |= O_WRONLY;
      break;
    case 2:
      host_flags |= O_RDWR;
      break;
    default:
      return -1;
  }

  if (guest_flags & 0x40) host_flags |= O_CREAT;
  if (guest_flags & 0x80) host_flags |= O_EXCL;
  if (guest_flags & 0x200) host_flags |= O_TRUNC;
  if (guest_flags & 0x400) host_flags |= O_APPEND;
  if (guest_flags & 0x800) host_flags |= O_NONBLOCK;
  if (guest_flags & 0x10000) host_flags |= O_DIRECTORY;
  if (guest_flags & 0x20000) host_flags |= O_NOFOLLOW;
  if (guest_flags & 0x80000) host_flags |= O_CLOEXEC;
  return host_flags;
}

static long lunix_read_string(uc_engine *uc, lunix_process_t *process, uint64_t addr, char *out, size_t out_size) {
  process=process;
  for (size_t i = 0; i < out_size - 1; i++) {
    uint8_t c;
    uc_err err = uc_mem_read(uc, addr + i, &c, 1);
    if (err != UC_ERR_OK) {
      return -EFAULT;
    }
    out[i] = (char)c;
    if (c == '\0') {
      return 0;
    }
  }

  out[out_size - 1] = '\0';
  return -ENAMETOOLONG;
}

// 17
static long lunix_sys_getcwd(uc_engine *uc, lunix_process_t *process, uint64_t buf_addr, uint64_t size) {
  process=process;
  char buf[4096];
  if (getcwd(buf, sizeof(buf)) == NULL) {
    return -errno;
  }
  
  size_t len = strlen(buf);
  if (len > size) {
    return -ERANGE;
  }

  uc_err err = uc_mem_write(uc, buf_addr, &buf, len);
  if (err != UC_ERR_OK) {
    return -EFAULT;
  }
  return buf_addr;
}

// 25
static long lunix_sys_fcntl(uc_engine *uc, lunix_process_t *process, int fd, int cmd, uint64_t arg) {
  process=process;uc=uc;
  switch (cmd) {
    case F_GETFD:
    case F_SETFD:
    case F_GETFL:
    case F_SETFL:
      return fcntl(fd, cmd, arg);
    default:
      return -EINVAL;
  }
}

// 29
static long lunix_sys_ioctl(uc_engine *uc, lunix_process_t *process, uint64_t fd, uint64_t cmd, uint64_t arg) {
  process=process;uc=uc;
  int result = ioctl(fd, cmd, arg);
  return result;
}

// 56
static long lunix_sys_openat(uc_engine *uc, lunix_process_t *process, int dirfd, uint64_t pathname_addr, int flags, mode_t mode) {
  process=process;
  char path[4096];
  long result = lunix_read_string(uc, process, pathname_addr, path, sizeof(path));
  if (result < 0) {
    return result;
  }
  path[sizeof(path) - 1] = '\0';

  flags = lunix_translate_open_flags(flags);
  int fd = openat(dirfd, path, flags, mode);
  if (fd < 0) {
    return -errno;
  }
  return fd;
}

// 57
static long lunix_sys_close(uc_engine *uc, lunix_process_t *process, int fd) {
  process=process;uc=uc;
  if (close(fd) < 0) {
    return -errno;
  }
  return 0;
}

// 61
static long lunix_sys_getdents64(uc_engine *uc, lunix_process_t *process, int fd, uint64_t dirp, unsigned int count) {
  process=process;
  char *buffer = malloc(count);
  if (!buffer) {
    return -ENOMEM;
  }

  long result = syscall(SYS_getdents64, fd, buffer, count);

  if (result < 0) {
    result = -errno;
    free(buffer);
    return result;
  }

  unsigned long pos = 0;
  unsigned long written = 0;

  while (pos < (unsigned long)result) {
    struct linux_dirent64 *host_entry =
      (struct linux_dirent64 *)(buffer + pos);

    size_t name_len = strlen(host_entry->d_name);

    size_t reclen =
      offsetof(struct linux_dirent64, d_name) +
      name_len + 1;

    reclen = (reclen + 7) & ~7UL;

    if (written + reclen > count) {
      break;
    }

    struct linux_dirent64 guest_entry = {
      .d_ino = host_entry->d_ino,
      .d_off = host_entry->d_off,
      .d_reclen = reclen,
      .d_type = host_entry->d_type
    };

    uc_err err = uc_mem_write(
      uc,
      dirp + written,
      &guest_entry,
      offsetof(struct linux_dirent64, d_name)
    );

    if (err != UC_ERR_OK) {
      free(buffer);
      return -EFAULT;
    }

    err = uc_mem_write(
      uc,
      dirp + written + offsetof(struct linux_dirent64, d_name),
      host_entry->d_name,
      name_len + 1
    );

    if (err != UC_ERR_OK) {
      free(buffer);
      return -EFAULT;
    }

    written += reclen;
    pos += host_entry->d_reclen;
  }

  free(buffer);
  return written;
}

// 62
static long lunix_sys_lseek(uc_engine *uc, lunix_process_t *process, int fd, off_t offset, int whence) {
  process=process;uc=uc;
  off_t result = lseek(fd, offset, whence);
  if (result == (off_t)-1) {
    return -errno;
  }
  return result;
}

// 63
static long lunix_sys_read(uc_engine *uc, lunix_process_t *process, int fd, uint64_t buf_addr, unsigned long count) {
  process=process;
  char *buf = malloc(count);
  ssize_t result = read(fd, buf, count);
  if (!result) {
    free(buf);
    return -ENOMEM;
  }

  uc_err err = uc_mem_write(uc, buf_addr, buf, count);
  free(buf);
  if (err != UC_ERR_OK) {
    return -EFAULT;
  }
  return result;
}

// 64
static long lunix_sys_write(uc_engine *uc, lunix_process_t *process, int fd, uint64_t buf, unsigned long count) {
  process=process;
  char *data = malloc(count + 1);
  if (!data) {
    return -12;
  }

  uc_err err = uc_mem_read(uc, buf, data, count);
  if (err != UC_ERR_OK) {
    free(data);
    return -14;
  }
  data[count] = '\0';
  
  long result = write(fd, data, count);
  free(data);
  return result;
}

// 71
static long lunix_sys_sendfile(uc_engine *uc, lunix_process_t *process, int out_fd, int in_fd, uint64_t offset_addr, uint64_t count) {
  process=process;
  int64_t offset;
  if (offset_addr != 0) {
    uc_err err = uc_mem_read(uc, offset_addr, &offset, sizeof(offset));
    if (err != UC_ERR_OK) {
      return -EFAULT;
    }
  }

  ssize_t result = sendfile(out_fd, in_fd, offset_addr ? &offset : NULL, count);
  if (result < 0) {
    return -errno;
  }

  if (offset_addr != 0) {
    uc_err err = uc_mem_write(uc, offset_addr, &offset, sizeof(offset));
    if (err != UC_ERR_OK) {
      return -EFAULT;
    }
  }
  return result;
}

// 78
static long lunix_sys_readlinkat(uc_engine *uc, lunix_process_t *process, int dirfd, uint64_t pathname, uint64_t buf, uint64_t len) {
  process=process;
  if (len == 0) {
    return 0;
  }

  char path[4096];
  long sresult = lunix_read_string(uc, process, pathname, path, sizeof(path));
  if (sresult < 0) {
    return sresult;
  }
  path[sizeof(path) - 1] = '\0';
  char *data = malloc(len);
  if (!data) {
    return -12;
  }

  ssize_t result = readlinkat(dirfd, path, data, len);
  if (result >= 0) {
    uc_err err = uc_mem_write(uc, buf, data, result);
    if (err != UC_ERR_OK) {
      result = -14;
    }
  }
  free(data);
  return result;
}

// 79
static long lunix_sys_newfstat(uc_engine *uc, lunix_process_t *process, int dirfd, uint64_t pathname_addr, uint64_t stat_addr, int flags) {
  process=process;
  char path[4096];
  long result = lunix_read_string(uc, process, pathname_addr, path, sizeof(path));
  if (result < 0) {
    return result;
  }
  path[sizeof(path) - 1] = '\0';

  struct stat host;
  if (fstatat(dirfd, path, &host, flags) < 0) {
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

  uc_err err = uc_mem_write(uc, stat_addr, &guest, sizeof(guest));
  if (err != UC_ERR_OK) {
    return -14;
  }
  return 0;
}

// 80
static long lunix_sys_fstat(uc_engine *uc, lunix_process_t *process, int fd, uint64_t stat_addr) {
  process=process;
  struct stat host;
  if (fstat(fd, &host) < 0) {
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

  uc_err err = uc_mem_write(uc, stat_addr, &guest, sizeof(guest));
  if (err != UC_ERR_OK) {
    return -14;
  }
  return 0;
}

// 93
static long lunix_sys_exit(uc_engine *uc, lunix_process_t *process, int status) {
  status=status;
  lunix_log("[lunix] exit: %d\n", status);
  process->exit_status = status;
  process->exited = true;
  uc_emu_stop(uc);
  return 0;
}

// 94
static long lunix_sys_exit_group(uc_engine *uc, lunix_process_t *process, int status) {
  status=status;
  process->exit_status = status;
  process->exited = true;
  uc_emu_stop(uc);
  return 0;
}

// 96
static long lunix_sys_set_tid_address(uc_engine *uc, lunix_process_t *process, uint64_t tidptr) {
  process=process;uc=uc;tidptr=tidptr;
  return 0;
}

// 99
static long lunix_sys_set_robust_list(uc_engine *uc, lunix_process_t *process, uint64_t head, uint64_t len) {
  process=process;uc=uc;head=head;len=len;
  return 0;
}

// 113
static long lunix_sys_clock_gettime(uc_engine *uc, lunix_process_t *process, int which_clock, uint64_t tp) {
  process=process;
  struct timespec ts;
  if (clock_gettime(which_clock, &ts) < 0) {
    return -errno;
  }

  uc_err err = uc_mem_write(uc, tp, &ts, sizeof(ts));
  if (err != UC_ERR_OK) {
    return -14;
  }
  return 0;
}

// 134
static long lunix_sys_rt_sigaction(uc_engine *uc, lunix_process_t *process, int sig, uint64_t act, uint64_t oldact, size_t sigsetsize) {
  process=process;uc=uc;sig=sig;act=act;oldact=oldact;sigsetsize=sigsetsize;
  return 0;
}

// 134
static long lunix_sys_rt_sigprocmask(uc_engine *uc, lunix_process_t *process, int how, uint64_t set, uint64_t oldset, size_t sigsetsize) {
  process=process;uc=uc;how=how;set=set;oldset=oldset;sigsetsize=sigsetsize;
  return 0;
}

// 144
static long lunix_sys_setgid(uc_engine *uc, lunix_process_t *process, uint64_t gid) {
  process=process;uc=uc;gid=gid;
  return 0;
}

// 145
static long lunix_sys_setregid(uc_engine *uc, lunix_process_t *process, uint64_t egid) {
  process=process;uc=uc;egid=egid;
  return 0;
}

// 146
static long lunix_sys_setuid(uc_engine *uc, lunix_process_t *process, uint64_t uid) {
  process=process;uc=uc;uid=uid;
  return 0;
}

// 147
static long lunix_sys_setreuid(uc_engine *uc, lunix_process_t *process, uint64_t euid) {
  process=process;uc=uc;euid=euid;
  return 0;
}

// 160
static long lunix_sys_uname(uc_engine *uc, lunix_process_t *process, uint64_t addr) {
  process=process;
  struct linux_utsname uts = {0};
  strcpy(uts.sysname, "Linux");
  strcpy(uts.nodename, "lunix");
  strcpy(uts.release, "6.16.7");
  strcpy(uts.version, "#1 lunix");
  strcpy(uts.machine, "aarch64");

  uc_err err = uc_mem_write(uc, addr, &uts, sizeof(uts));
  if (err != UC_ERR_OK) {
    return -EFAULT;
  }
  return 0;
}

// 172
static long lunix_sys_getpid(uc_engine *uc, lunix_process_t *process) {
  uc=uc;
  return process->pid;
}

// 173
static long lunix_sys_getppid(uc_engine *uc, lunix_process_t *process) {
  uc=uc;
  return process->ppid;
}

// 174
static long lunix_sys_getuid(uc_engine *uc, lunix_process_t *process) {
  uc=uc;
  return process->uid;
}

// 175
static long lunix_sys_geteuid(uc_engine *uc, lunix_process_t *process) {
  uc=uc;
  return process->uid;
}

// 176
static long lunix_sys_getgid(uc_engine *uc, lunix_process_t *process) {
  uc=uc;
  return process->gid;
}

// 177
static long lunix_sys_getegid(uc_engine *uc, lunix_process_t *process) {
  uc=uc;
  return process->gid;
}

// 214
static long lunix_sys_brk(uc_engine *uc, lunix_process_t *process, uint64_t address) {
  process=process;uc=uc;
  if (address == 0) {
    return heap_end;
  }
  if (address < LUNIX_HEAP_BASE || address > LUNIX_HEAP_BASE + LUNIX_HEAP_SIZE) {
    return heap_end;
  }
  heap_end = address;
  return heap_end;
}

// 220
static long lunix_sys_clone(uc_engine *uc, lunix_process_t *process, uint64_t flags, uint64_t newsp, int ptid, int ctid, uint64_t tls) {
  uc=uc;flags=flags;ptid=ptid;ctid=ctid;tls=tls;
  if (newsp == 0) {
    newsp = process->sp;
  }
  return lunix_process_manager_create(process->uc, process->pid, newsp, process->argc, process->argv);
}

// 222
static long lunix_sys_mmap(uc_engine *uc, lunix_process_t *process, uint64_t addr, uint64_t len, int prot, int flags, int fd, off_t offset) {
  process=process;flags=flags;fd=fd;offset=offset;
  static uint64_t mmap_next = LUNIX_MMAP_BASE;
  if (len == 0) return -EINVAL;

  uint64_t size = (len + 0xfffULL) & ~0xfffULL;
  uint64_t start = addr & ~0xfffULL;

  if (addr == 0)
    start = mmap_next;

  uint64_t end = start + size;
  if (end <= start) return -EINVAL;

  int perms = 0;
  if (prot & PROT_READ) perms |= UC_PROT_READ;
  if (prot & PROT_WRITE) perms |= UC_PROT_WRITE;
  if (prot & PROT_EXEC) perms |= UC_PROT_EXEC;

  uc_err err = uc_mem_map(uc, start, size, perms);
  if (err != UC_ERR_OK) {
    lunix_log("[lunix] mmap: start=0x%lx size=0x%lx prot=0x%x flags=0x%x\n", start, size, prot, flags);
    lunix_log("[lunix] mmap: %s\n", uc_strerror(err));
    return -ENOMEM;
  }

  if (addr == 0)
    mmap_next = end;

  return start;
}

// 226
static long lunix_sys_mprotect(uc_engine *uc, lunix_process_t *process, uint64_t addr, uint64_t len, int prot) {
  process=process;
  uint64_t start = addr & ~0xfffULL;
  uint64_t end = (addr + len + 0xfff) & ~0xfffULL;
  if (end <= start) {
    return -22;
  }

  int perms = 0;
  if (prot & PROT_READ) perms |= UC_PROT_READ;
  if (prot & PROT_WRITE) perms |= UC_PROT_WRITE;
  if (prot & PROT_EXEC) perms |= UC_PROT_EXEC;

  uc_err err = uc_mem_protect(uc, start, end - start, perms);
  if (err != UC_ERR_OK) {
    lunix_log("[lunix] mprotect: %s\n", uc_strerror(err));
    return -22;
  }

  return 0;
}

// 233
static long lunix_sys_madvise(uc_engine *uc, lunix_process_t *process, uint64_t addr, uint64_t len, int advice) {
  process=process;uc=uc;addr=addr;len=len;advice=advice;
  return 0;
}

// 261
static long lunix_sys_prlimit64(uc_engine *uc, lunix_process_t *process, uint64_t pid, uint64_t resource, uint64_t new_limit, uint64_t old_limit) {
  process=process;uc=uc;pid=pid;resource=resource;new_limit=new_limit;old_limit=old_limit;
  return 0;
}

// 278
static long lunix_sys_getrandom(uc_engine *uc, lunix_process_t *process, uint64_t buf, uint64_t len, unsigned int flags) {
  process=process;
  uint8_t *data = malloc(len);
  if (!data) {
    return -12;
  }
  
  ssize_t result = getrandom(data, len, flags);
  if (result < 0) {
    free(data);
    return -1;
  }
  
  uc_err err = uc_mem_write(uc, buf, data, result);
  if (err != UC_ERR_OK) {
    return -14;
  }

  free(data);
  return result;
}

// 293
static long lunix_sys_rseq(uc_engine *uc, lunix_process_t *process, uint64_t rseq, uint64_t rseq_len, uint64_t flags, uint64_t sig) {
  process=process;uc=uc;rseq=rseq;rseq_len=rseq_len;flags=flags;sig=sig;
  return 0;
}

long lunix_syscall(uc_engine *uc, lunix_process_t *process) {
  uint64_t number;
  uint64_t r0;
  uint64_t r1;
  uint64_t r2;
  uint64_t r3;

  uc_reg_read(uc, UC_ARM64_REG_X8, &number);
  uc_reg_read(uc, UC_ARM64_REG_X0, &r0);
  uc_reg_read(uc, UC_ARM64_REG_X1, &r1);
  uc_reg_read(uc, UC_ARM64_REG_X2, &r2);
  uc_reg_read(uc, UC_ARM64_REG_X3, &r3);

  lunix_debug("[lunix] number: %lu\n", number);
  lunix_debug("[lunix] r0: %lu\n", r0);
  lunix_debug("[lunix] r1: %lu\n", r1);
  lunix_debug("[lunix] r2: %lu\n", r2);
  lunix_debug("[lunix] r3: %lu\n", r3);

  switch (number) {
    case 17:
      return lunix_sys_getcwd(uc, process, r0, r1);

    case 25:
      return lunix_sys_fcntl(uc, process, (int)r0, (int)r1, r2);

    case 29:
      return lunix_sys_ioctl(uc, process, r0, r1, r2);

    case 56:
      return lunix_sys_openat(uc, process, (int)r0, r1, (int)r2, (mode_t)r3);

    case 57:
      return lunix_sys_close(uc, process, (int)r0);

    case 61:
      return lunix_sys_getdents64(uc, process, r0, r1, r2);

    case 62:
      return lunix_sys_lseek(uc, process, (int)r0, (off_t)r1, (int)r2);

    case 63:
      return lunix_sys_read(uc, process, (int)r0, r1, r2);

    case 64:
      return lunix_sys_write(uc, process, (int)r0, r1, r2);

    case 71:
      return lunix_sys_sendfile(uc, process, (int)r0, (int)r1, (int64_t)r2, (uint64_t)r3);

    case 78:
      return lunix_sys_readlinkat(uc, process, (int)r0, r1, r2, r3);
   
    case 79:
      return lunix_sys_newfstat(uc, process, (int)r0, r1, r2, (int)r3);

    case 80:
      return lunix_sys_fstat(uc, process, (int)r0, r1);

    case 93:
      return lunix_sys_exit(uc, process, (int)r0);

    case 94:
      return lunix_sys_exit_group(uc, process, (int)r0);

    case 96:
      return lunix_sys_set_tid_address(uc, process, r0);

    case 99:
      return lunix_sys_set_robust_list(uc, process, r0, r1);

    case 113:
      return lunix_sys_clock_gettime(uc, process, (int)r0, r1);

    case 134:
      return lunix_sys_rt_sigaction(uc, process, (int)r0, r1, r2, r3);
    
    case 135:
      return lunix_sys_rt_sigprocmask(uc, process, (int)r0, r1, r2, r3);

    case 144:
      return lunix_sys_setgid(uc, process, r0);

    case 145:
      return lunix_sys_setregid(uc, process, r0);

    case 146:
      return lunix_sys_setuid(uc, process, r0);

    case 147:
      return lunix_sys_setreuid(uc, process, r0);

    case 160:
      return lunix_sys_uname(uc, process, r0);    

    case 172:
      return lunix_sys_getpid(uc, process);
    
    case 173:
      return lunix_sys_getppid(uc, process);

    case 174:
      return lunix_sys_getuid(uc, process);

    case 175:
      return lunix_sys_geteuid(uc, process);

    case 176:
      return lunix_sys_getgid(uc, process);

    case 177:
      return lunix_sys_getegid(uc, process);

    case 214:
      return lunix_sys_brk(uc, process, r0);

    case 222:
      uint64_t r4;
      uint64_t r5;
      uc_reg_read(uc, UC_ARM64_REG_X4, &r4);
      uc_reg_read(uc, UC_ARM64_REG_X5, &r5);
      lunix_debug("[lunix] r4: %lu\n", r4);
      lunix_debug("[lunix] r5: %lu\n", r5);
      return lunix_sys_mmap(uc, process, r0, r1, (int)r2, (int)r3, (int)r4, (off_t)r5);

    case 220:
      return lunix_sys_clone(uc, process, r0, r1, (int)r2, (int)r3, r4);

    case 226:
      return lunix_sys_mprotect(uc, process, r0, r1, r2);
    
    case 233:
      return lunix_sys_madvise(uc, process, r0, r1, (int)r2);

    case 261:
      return lunix_sys_prlimit64(uc, process, r0, r1, r2, r3);  

    case 278:
      return lunix_sys_getrandom(uc, process, r0, r1, (unsigned int)r2);

    case 293:
      return lunix_sys_rseq(uc, process, r0, r1, r2, r3);

    default:
      lunix_log("[lunix] unimplemented syscall: %lu\n", number);
      return -38;
  }
}
