#ifndef _KERNEL_FD_H
#define _KERNEL_FD_H

#include <kernel/types.h>
#include <kernel/fs.h>

#define MAXFD 64
#define O_NONBLOCK 0x800
#define O_CLOEXEC  02000000

struct fdent {
  int used;
  struct fs_file f;
  char path[128];
  int dir_idx;
  int flags;
};

extern struct fdent fdtables[64][MAXFD];
extern int64_t sys_closefd(int fd);
extern void close_cloexec_fds(int pid);

#endif
