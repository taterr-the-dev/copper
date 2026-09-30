#ifndef _KERNEL_SYSCALL_H
#define _KERNEL_SYSCALL_H

#include <kernel/types.h>

#define SYS_READ 0
#define SYS_WRITE 1
#define SYS_OPEN 2
#define SYS_CLOSE 3
#define SYS_STAT 4
#define SYS_FSTAT 5
#define SYS_LSTAT 6
#define SYS_LSEEK 8
#define SYS_MMAP 9
#define SYS_MPROTECT 10
#define SYS_MUNMAP 11
#define SYS_BRK 12
#define SYS_RT_SIGACTION 13
#define SYS_RT_SIGPROCMASK 14
#define SYS_IOCTL 16
#define SYS_WRITEV 20
#define SYS_ACCESS 21
#define SYS_PIPE 22
#define SYS_DUP 32
#define SYS_DUP2 33
#define SYS_NANOSLEEP 35
#define SYS_GETPID 39
#define SYS_SOCKET 41
#define SYS_CLONE 56
#define SYS_FORK 57
#define SYS_EXECVE 59
#define SYS_EXIT 60
#define SYS_WAIT4 61
#define SYS_KILL 62
#define SYS_UNAME 63
#define SYS_FCNTL 72
#define SYS_GETCWD 79
#define SYS_CHDIR 80
#define SYS_MKDIR 83
#define SYS_RMDIR 84
#define SYS_UNLINK 87
#define SYS_GETTIMEOFDAY 96
#define SYS_GETUID 102
#define SYS_GETGID 104
#define SYS_GETEUID 107
#define SYS_GETEGID 108
#define SYS_GETPPID 110
#define SYS_SETSID 112
#define SYS_MOUNT 165
#define SYS_UMOUNT 166
#define SYS_ARCH_PRCTL 158
#define SYS_GETTID 186
#define SYS_GETDENTS64 217
#define SYS_CLOCK_GETTIME 228
#define SYS_EXIT_GROUP 231
#define SYS_UTIMES 235
#define SYS_NEWFSTATAT 262
#define SYS_UNLINKAT 263
#define SYS_FACCESSAT 269
#define SYS_SET_TID_ADDR 218
#define SYS_SET_ROBUST_LIST 273
#define SYS_UTIMENSAT 280
#define SYS_DUP3 292
#define SYS_PIPE2 293
#define SYS_PRLIMIT64 302
#define SYS_GETRANDOM 318
#define SYS_STATX 332
#define SYS_OPENAT 257
#define SYS_FACCESSAT2 439

#define EPERM 1
#define ENOENT 2
#define ESRCH 3
#define EINTR 4
#define EIO 5
#define ENXIO 6
#define E2BIG 7
#define ENOEXEC 8
#define EBADF 9
#define ECHILD 10
#define EAGAIN 11
#define ENOMEM 12
#define EACCES 13
#define EFAULT 14
#define ENOTBLK 15
#define EBUSY 16
#define EEXIST 17
#define EXDEV 18
#define ENODEV 19
#define ENOTDIR 20
#define EISDIR 21
#define EINVAL 22
#define ENFILE 23
#define EMFILE 24
#define ENOTTY 25
#define ETXTBSY 26
#define EFBIG 27
#define ENOSPC 28
#define ESPIPE 29
#define EROFS 30
#define EMLINK 31
#define EPIPE 32
#define ENAMETOOLONG 36
#define ERANGE 34
#define ENOSYS 38
#define ENOTEMPTY 39
#define ENOTSOCK 88
#define ESOCKTNOSUPPORT 94
#define EAFNOSUPPORT 97
#define ECONNREFUSED 111
#define ENOTSUP 134

struct pt_regs {
    uint64_t rax;
    uint64_t rbx;
    uint64_t rcx;
    uint64_t rdx;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t rbp;
    uint64_t r8;
    uint64_t r9;
    uint64_t r10;
    uint64_t r11;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
    uint64_t saved_rflags;
    uint64_t saved_rip;
    uint64_t saved_rsp;
};

struct iovec {
    void *iov_base;
    size_t iov_len;
};

struct timeval {
    int64_t tv_sec, tv_usec;
};

struct timespec {
    int64_t tv_sec, tv_nsec;
};

struct utsname {
    char sysname[65], nodename[65], release[65], version[65], machine[65], domainname[65];
};

struct statx_timestamp {
    int64_t tv_sec;
    uint32_t tv_nsec;
    int32_t __reserved;
};

struct statx {
    uint32_t stx_mask;
    uint32_t stx_blksize;
    uint64_t stx_attributes;
    uint32_t stx_nlink;
    uint32_t stx_uid;
    uint32_t stx_gid;
    uint16_t stx_mode;
    uint16_t __spare0;
    uint64_t stx_ino;
    uint64_t stx_size;
    uint64_t stx_blocks;
    uint64_t stx_attributes_mask;
    struct statx_timestamp stx_atime;
    struct statx_timestamp stx_btime;
    struct statx_timestamp stx_ctime;
    struct statx_timestamp stx_mtime;
    uint32_t stx_rdev_major;
    uint32_t stx_rdev_minor;
    uint32_t stx_dev_major;
    uint32_t stx_dev_minor;
    uint64_t __spare2[14];
};

void syscall_init(void);
int64_t syscall_dispatch(struct pt_regs *regs);
void syscall_selftest(void);
void setup_std_fds(int pid);
void resolve_path(const char *p, char *out, size_t out_size);

#endif
