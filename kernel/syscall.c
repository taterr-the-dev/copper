#include <autoconf.h>
#include <kernel/blkdev.h>
#include <kernel/console.h>
#include <kernel/kmalloc.h>
#include <kernel/msr.h>
#include <kernel/proc.h>
#include <kernel/string.h>
#include <kernel/syscall.h>
extern uint64_t saved_rsp;
#include <kernel/fs.h>
#ifdef CONFIG_VM
#include <kernel/pmm.h>
#include <kernel/vm.h>
#endif
#ifdef CONFIG_TTY
#include <kernel/tty.h>
struct tty *console_tty = NULL;
int console_tty_write(struct tty *t, const char *b, size_t l) {
  (void)t;
  for (size_t i = 0; i < l; i++)
    con_putc(b[i]);
  return l;
}
#endif
#define BRK_BASE 0x0000100000000000ULL
#define O_CREAT 00000100
#define O_EXCL 00000200
#define O_TRUNC 00001000
#define O_WRONLY 00000001
#define O_RDWR 00000002
#define O_APPEND 00002000
#ifdef CONFIG_NET
extern int64_t sys_setsockopt(int, int, int, const void *, int);
extern int64_t sys_socket(int, int, int);
extern int64_t sys_bind(int, const void *, int);
extern int64_t sys_sendto(int, const void *, size_t, int, const void *, int);
extern int64_t sys_recvfrom(int, void *, size_t, int, void *, int *);
extern int64_t sys_connect(int, const void *, int);
extern int64_t sys_listen(int, int);
extern int64_t sys_accept(int, void *, int *);
extern int64_t sys_tcp_read(int, void *, size_t);
extern int64_t sys_tcp_write(int, const void *, size_t);
#endif
extern char percpu_data[];
extern int kbd_read(char *, int);
extern void syscall_entry(void);
extern void hex64(uint64_t v);
extern uint64_t current_kstack;
static inline uint64_t rdtsc(void) {
  uint32_t a, d;
  __asm__ volatile("rdtsc" : "=a"(a), "=d"(d));
  return ((uint64_t)d << 32) | a;
}
void put_u64(uint64_t v) {
  char t[21];
  int i = 0;
  if (!v)
    t[i++] = '0';
  while (v) {
    t[i++] = '0' + v % 10;
    v /= 10;
  }
  while (i)
    con_putc(t[--i]);
}
static void copy_str(char *d, const char *s, size_t n) {
  size_t i = 0;
  for (; i + 1 < n && s[i]; i++)
    d[i] = s[i];
  for (; i < n; i++)
    d[i] = 0;
}
uint64_t debug_syscall_ret;
struct pipe_buf {
  int refs;
  char data[4096];
  int read_pos;
  int write_pos;
  int count;
  int write_closed;
  int read_refs;
  int write_refs;
};

#define USER_SPACE_LIMIT 0x0000800000000000ULL
static inline bool access_ok(const void *ptr, size_t size) {
  uint64_t addr = (uint64_t)ptr;
  if (size == 0)
    return true;
  if (addr >= USER_SPACE_LIMIT)
    return false;
  if (addr + size > USER_SPACE_LIMIT)
    return false;
  return true;
}

static size_t safe_strnlen(const char *s, size_t maxlen) {
  size_t len = 0;
  while (len < maxlen && s[len] != '\0')
    len++;
  return len;
}

static int64_t sys_getpid(void) { return current ? current->pid : 0; }
static int64_t sys_gettid(void) { return current ? current->pid : 0; }
static int64_t sys_getppid(void) {
  return current && current->parent ? current->parent->pid : 0;
}
static int64_t sys_getuid(void) { return 0; }
static int64_t sys_getgid(void) { return 0; }
static int64_t sys_geteuid(void) { return 0; }
static int64_t sys_getegid(void) { return 0; }
static int64_t sys_setsid(void) { return 1; }

static char cwd_table[64][256];
static int cwd_initialized = 0;

static void cwd_init(void) {
  if (cwd_initialized)
    return;
  for (int i = 0; i < 64; i++) {
    cwd_table[i][0] = '/';
    cwd_table[i][1] = '\0';
  }
  cwd_initialized = 1;
}

static inline char *cwd_of(void) {
  cwd_init();
  int p = current ? current->pid : 0;
  if (p < 0 || p >= 64)
    p = 0;
  return cwd_table[p];
}

static void resolve_path(const char *p, char *out, size_t out_size) {
  if (!access_ok(p, 1)) {
    out[0] = 0;
    return;
  }
  size_t p_len = safe_strnlen(p, out_size);
  if (p_len >= out_size) {
    out[0] = 0;
    return;
  }

  if (p[0] == '/') {
    copy_str(out, p, out_size);
    return;
  }

  char *cwd = cwd_of();
  size_t cwd_len = strlen(cwd);

  if (cwd_len == 1 && cwd[0] == '/') {
    if (1 + p_len >= out_size) {
      out[0] = 0;
      return;
    }
    out[0] = '/';
    memcpy(out + 1, p, p_len + 1);
  } else {
    if (cwd_len + 1 + p_len >= out_size) {
      out[0] = 0;
      return;
    }
    memcpy(out, cwd, cwd_len);
    out[cwd_len] = '/';
    memcpy(out + cwd_len + 1, p, p_len + 1);
  }
}

#define MAXFD 64
struct fdent {
  int used;
  struct fs_file f;
  char path[128];
  int dir_idx;
  int flags;
};
static struct fdent fdtables[64][MAXFD];
struct fdent *fdtable_of(void) {
  int p = current ? current->pid : 0;
  if (p < 0 || p >= 64)
    p = 0;
  return fdtables[p];
}
#define fdtable (fdtable_of())
const char *proc_get_fd_path(int fd) {
  if (fd < 0 || fd >= MAXFD)
    return NULL;
  struct fdent *ft = fdtable_of();
  if (!ft[fd].used)
    return NULL;
  return ft[fd].path;
}
static int fd_alloc(void) {
  for (int i = 3; i < MAXFD; i++)
    if (!fdtable[i].used) {
      fdtable[i].used = 1;
      return i;
    }
  return -EMFILE;
}

int proc_get_task_fd_path(int pid, int fd, char *buf, size_t sz) {
  if (pid < 0 || pid >= 64)
    return -1;
  if (fd < 0 || fd >= MAXFD)
    return -1;
  if (!fdtables[pid][fd].used)
    return -1;

  strncpy(buf, fdtables[pid][fd].path, sz);
  buf[sz - 1] = '\0';
  return 0;
}

void setup_std_fds(int pid) {
  if (pid < 0 || pid >= 64)
    return;

#ifdef CONFIG_TTY
  if (!console_tty) {
    static struct tty_ops console_ops = {.write = (void *)console_tty_write};
    console_tty = tty_alloc("console", &console_ops);
  }
#endif

  struct fs_file f;
  if (vfs_open("/dev/stdin", &f) == 0) {
    fdtables[pid][0].f = f;
#ifdef CONFIG_TTY
    fdtables[pid][0].f.tty = console_tty;
#endif
    fdtables[pid][0].used = 1;
    copy_str(fdtables[pid][0].path, "/dev/stdin", 128);
    fdtables[pid][0].dir_idx = 0;
  }
  if (vfs_open("/dev/stdout", &f) == 0) {
    fdtables[pid][1].f = f;
#ifdef CONFIG_TTY
    fdtables[pid][1].f.tty = console_tty;
#endif
    fdtables[pid][1].used = 1;
    copy_str(fdtables[pid][1].path, "/dev/stdout", 128);
    fdtables[pid][1].dir_idx = 0;
  }
  if (vfs_open("/dev/stderr", &f) == 0) {
    fdtables[pid][2].f = f;
#ifdef CONFIG_TTY
    fdtables[pid][2].f.tty = console_tty;
#endif
    fdtables[pid][2].used = 1;
    copy_str(fdtables[pid][2].path, "/dev/stderr", 128);
    fdtables[pid][2].dir_idx = 0;
  }
}

static int64_t sys_openat(int dfd, const char *p, int fl, int mo) {
  (void)dfd;
  (void)mo;

  char full_path[256];
  resolve_path(p, full_path, sizeof(full_path));
  if (strcmp(full_path, "/dev/ptmx") == 0) {
    int i = fd_alloc();
    if (i < 0)
      return i;

    struct tty *master = pty_get_master();
    if (!master) {
      fdtable[i].used = 0;
      return -ENOMEM;
    }

    fdtable[i].used = 1;
    fdtable[i].f.tty = master;
    fdtable[i].f.mode = 0020666;
    copy_str(fdtable[i].path, "/dev/ptmx", 128);
    return i;
  }
  int i = fd_alloc();
  if (i < 0)
    return i;
  memset(&fdtable[i], 0, sizeof(struct fdent));
  fdtable[i].used = 1;
  fdtable[i].flags = fl;

#ifdef CONFIG_TTY
  if (strcmp(full_path, "/dev/ptmx") == 0) {
    struct tty *master = pty_get_master();
    if (!master) {
      fdtable[i].used = 0;
      return -ENOMEM;
    }
    fdtable[i].f.tty = master;
    fdtable[i].f.mode = 0020666;
    copy_str(fdtable[i].path, full_path, 128);
    return i;
  }
  if (strcmp(full_path, "/dev/console") == 0 ||
      strcmp(full_path, "/dev/tty") == 0 ||
      strcmp(full_path, "/dev/stdin") == 0 ||
      strcmp(full_path, "/dev/stdout") == 0 ||
      strcmp(full_path, "/dev/stderr") == 0) {
    if (!console_tty) {
      extern int console_tty_write(struct tty * t, const char *b, size_t l);
      static struct tty_ops console_ops = {.write = (void *)console_tty_write};
      console_tty = tty_alloc("console", &console_ops);
    }
    if (console_tty) {
      fdtable[i].f.tty = console_tty;
    }
  }
#endif

  if (fl & O_CREAT) {
    int cr = vfs_create(full_path);
    if (cr != 0) {
      if (cr == -EEXIST && !(fl & O_EXCL)) {
      } else {
        fdtable[i].used = 0;
        return cr;
      }
    }
  }

  if (vfs_open(full_path, &fdtable[i].f)) {
    fdtable[i].used = 0;
    return -ENOENT;
  }

#ifdef CONFIG_TTY
  if (strcmp(full_path, "/dev/stdin") == 0 ||
      strcmp(full_path, "/dev/stdout") == 0 ||
      strcmp(full_path, "/dev/stderr") == 0 ||
      strcmp(full_path, "/dev/console") == 0 ||
      strcmp(full_path, "/dev/tty") == 0) {
    if (!console_tty) {
      static struct tty_ops console_ops = {.write = (void *)console_tty_write};
      console_tty = tty_alloc("console", &console_ops);
    }
    fdtable[i].f.tty = console_tty;
  }
#endif

  copy_str(fdtable[i].path, full_path, 128);
  fdtable[i].dir_idx = 0;
  return i;
}
static int64_t sys_readfd(int fd, void *b, size_t n) {
  if (fd < 0 || fd >= MAXFD || !fdtable[fd].used)
    return -EBADF;
  if (!access_ok(b, n))
    return -EFAULT;
#ifdef CONFIG_TTY
  if (fdtable[fd].f.tty) {
    int ret = tty_read((struct tty *)fdtable[fd].f.tty, (char *)b, n);
    return ret;
  }
#endif
  if (strcmp(fdtable[fd].path, "/pipe/read") == 0) {
    struct pipe_buf *pb = fdtable[fd].f.priv;
    if (!pb)
      return 0;
    while (pb->count == 0) {
      if (pb->write_closed) {
        return 0;
      }
      yield();
    }

    size_t to_read = n;
    if (to_read > (size_t)pb->count)
      to_read = pb->count;

    char *dst = (char *)b;
    for (size_t i = 0; i < to_read; i++) {
      dst[i] = pb->data[pb->read_pos];
      pb->read_pos = (pb->read_pos + 1) % 4096;
    }
    pb->count -= to_read;
    return to_read;
  }

#ifdef CONFIG_NET
  if (strncmp(fdtable[fd].path, "/net/tcp/", 9) == 0) {
    return sys_tcp_read(fd, b, n);
  }
#endif
  int64_t ret = vfs_read(&fdtable[fd].f, b, n);
  return ret;
}

static int64_t sys_writefd(int fd, const char *b, size_t n) {
  if (fd < 0 || fd >= MAXFD || !fdtable[fd].used)
    return -EBADF;
  if (!access_ok(b, n))
    return -EFAULT;
#ifdef CONFIG_TTY
  if (fdtable[fd].f.tty) {
    return tty_write((struct tty *)fdtable[fd].f.tty, b, n);
  }
#endif
  if (strcmp(fdtable[fd].path, "/pipe/write") == 0) {
    struct pipe_buf *pb = fdtable[fd].f.priv;
    if (!pb)
      return -EPIPE;
    size_t written = 0;
    while (written < n) {
      while (pb->count == 4096) {
        yield();
      }

      size_t space = 4096 - pb->count;
      size_t to_write = n - written;
      if (to_write > space)
        to_write = space;

      const char *src = b + written;
      for (size_t i = 0; i < to_write; i++) {
        pb->data[pb->write_pos] = src[i];
        pb->write_pos = (pb->write_pos + 1) % 4096;
      }
      pb->count += to_write;
      written += to_write;
    }
    return written;
  }

  if (fdtable[fd].flags & O_APPEND) {
    struct fs_file temp_f;
    if (vfs_open(fdtable[fd].path, &temp_f) == 0) {
      fdtable[fd].f.pos = temp_f.size;
      fdtable[fd].f.size = temp_f.size;
      vfs_close(&temp_f);
    } else {
      fdtable[fd].f.pos = fdtable[fd].f.size;
    }
  }

#ifdef CONFIG_NET
  if (strncmp(fdtable[fd].path, "/net/tcp/", 9) == 0) {
    return sys_tcp_write(fd, b, n);
  }
#endif
  return vfs_write(&fdtable[fd].f, b, n);
}

void cleanup_process_fds(int pid) {
  if (pid < 0 || pid >= 64)
    return;

  for (int i = 0; i < MAXFD; i++) {
    if (fdtables[pid][i].used) {
      if (strcmp(fdtables[pid][i].path, "/pipe/read") == 0) {
        struct pipe_buf *pb = fdtables[pid][i].f.priv;
        if (pb)
          pb->read_refs--;
      }
      if (strcmp(fdtables[pid][i].path, "/pipe/write") == 0) {
        struct pipe_buf *pb = fdtables[pid][i].f.priv;
        if (pb) {
          pb->write_refs--;
          if (pb->write_refs == 0) {
            pb->write_closed = 1;
          }
        }
      }
      vfs_close(&fdtables[pid][i].f);
      fdtables[pid][i].used = 0;
    }
  }
}

static int64_t sys_closefd(int fd) {
  if (fd < 3)
    return 0;
  if (!fdtable[fd].used)
    return -EBADF;
  if (strcmp(fdtable[fd].path, "/pipe/read") == 0 ||
      strcmp(fdtable[fd].path, "/pipe/write") == 0) {
    struct pipe_buf *pb = fdtable[fd].f.priv;
    if (pb) {
      if (strcmp(fdtable[fd].path, "/pipe/write") == 0) {
        pb->write_refs--;
        if (pb->write_refs == 0)
          pb->write_closed = 1;
      } else {
        pb->read_refs--;
      }
      pb->refs--;
      if (pb->refs == 0)
        kfree(pb);
      fdtable[fd].f.priv = NULL;
    }
  }
#ifdef CONFIG_NET
  if (strncmp(fdtable[fd].path, "/net/tcp/", 9) == 0) {
    extern void tcp_close(int);
    extern int tcp_get_pcb(int);
    int pcb = tcp_get_pcb(fd);
    if (pcb >= 0)
      tcp_close(pcb);
  }
#endif
  fdtable[fd].used = 0;
  vfs_close(&fdtable[fd].f);
  return 0;
}
static int64_t sys_lseekfd(int fd, long o, int w) {
  if (fd < 3 || !fdtable[fd].used)
    return -EBADF;
  struct fs_file *f = &fdtable[fd].f;
  if (w == 0)
    f->pos = o;
  else if (w == 1)
    f->pos += o;
  else
    f->pos = f->size + o;
  return f->pos;
}
static int64_t sys_fstatfd(int fd, void *st) {
  if (!access_ok(st, 144))
    return -EFAULT;

  if (fd >= 0 && fd < 3) {
    uint8_t k_st[144];
    memset(k_st, 0, 144);
    uint8_t *u = k_st;
    uint32_t mode = 0020666;
    memcpy(u + 24, &mode, 4);
    uint64_t sz = 0;
    memcpy(u + 48, &sz, 8);
    uint64_t blksize = 4096;
    memcpy(u + 56, &blksize, 8);
    uint64_t blk = 0;
    memcpy(u + 64, &blk, 8);
    for (int i = 0; i < 144; i++)
      ((uint8_t *)st)[i] = k_st[i];
    return 0;
  }

  if (!fdtable[fd].used)
    return -EBADF;

  uint8_t k_st[144];
  memset(k_st, 0, 144);
  uint8_t *u = k_st;

  uint32_t mode = fdtable[fd].f.mode;
  if (mode == 0)
    mode = 0100644;

  memcpy(u + 24, &mode, 4);
  uint64_t sz = fdtable[fd].f.size;
  memcpy(u + 48, &sz, 8);
  uint64_t blksize = 4096;
  memcpy(u + 56, &blksize, 8);
  uint64_t blk = (sz + 511) / 512;
  memcpy(u + 64, &blk, 8);

  for (int i = 0; i < 144; i++)
    ((uint8_t *)st)[i] = k_st[i];
  return 0;
}

static int64_t sys_writevfd(int fd, const struct iovec *iov, int c) {
  int64_t t = 0;
  for (int i = 0; i < c; i++)
    t += sys_writefd(fd, iov[i].iov_base, iov[i].iov_len);
  return t;
}

static inline void inc_priv_refs(struct fs_file *f) {
  if (f->priv) {
    if ((uint64_t)f->priv > 0x10000) {
      int *refs = (int *)f->priv;
      (*refs)++;
    }
  }
}

static int64_t sys_dupfd(int oldfd) {
  if (oldfd < 0 || oldfd >= MAXFD || !fdtable[oldfd].used)
    return -EBADF;
  int i = fd_alloc();
  if (i < 0)
    return i;

  fdtable[i] = fdtable[oldfd];
  inc_priv_refs(&fdtable[i].f);
  if (strcmp(fdtable[i].path, "/pipe/read") == 0) {
    struct pipe_buf *pb = fdtable[i].f.priv;
    if (pb)
      pb->read_refs++;
  } else if (strcmp(fdtable[i].path, "/pipe/write") == 0) {
    struct pipe_buf *pb = fdtable[i].f.priv;
    if (pb)
      pb->write_refs++;
  }

  return i;
}

static int64_t sys_fcntl(int fd, int cmd, uint64_t arg) {
  if (fd < 0 || fd >= MAXFD || !fdtable[fd].used)
    return -EBADF;

  if (cmd == 0)
    return sys_dupfd(fd);
  if (cmd == 1)
    return 0;
  if (cmd == 2)
    return 0;
  if (cmd == 3)
    return fdtable[fd].flags;
  if (cmd == 4) {
    fdtable[fd].flags = arg;
    return 0;
  }
  return 0;
}

static int64_t sys_mount(const char *source, const char *target,
                         const char *fstype) {
  (void)source;
  (void)fstype;
  struct blkdev *dev = blkdev_get(0);
  if (!dev)
    return -ENODEV;
  extern int fs_try_mount(struct blkdev * d, struct fs_ops * *out_ops,
                          void **out_sb);
  struct fs_ops *ops = NULL;
  void *sb = NULL;
  if (fs_try_mount(dev, &ops, &sb) != 0) {
    return -EINVAL;
  }
  return vfs_mount_at(target, ops, sb);
}

static int64_t sys_umount(const char *target) { return vfs_umount(target); }

static int64_t sys_unlink(const char *path) {
  char abs_path[256];
  resolve_path(path, abs_path, sizeof(abs_path));
  return vfs_unlink(abs_path);
}

static int64_t sys_unlinkat(int dfd, const char *path, int flags) {
  (void)dfd;
  char abs_path[256];
  resolve_path(path, abs_path, sizeof(abs_path));
  if (flags & 0x200) {
    return vfs_rmdir(abs_path);
  }
  return vfs_unlink(abs_path);
}

static int64_t sys_mkdir(const char *path, uint32_t mode) {
  char abs_path[256];
  resolve_path(path, abs_path, sizeof(abs_path));
  return vfs_mkdir(abs_path, mode);
}

static int64_t sys_rmdir(const char *path) {
  char abs_path[256];
  resolve_path(path, abs_path, sizeof(abs_path));
  return vfs_rmdir(abs_path);
}

static int64_t stat_path(const char *p, void *st) {
  if (!access_ok(st, 144))
    return -EFAULT;
  char abs_path[256];
  resolve_path(p, abs_path, sizeof(abs_path));

  struct fs_file f;
  if (vfs_open(abs_path, &f) != 0) {
    return -ENOENT;
  }

  uint8_t k_st[144];
  memset(k_st, 0, 144);
  uint8_t *u = k_st;

  uint32_t mode = f.mode;
  if (mode == 0) {
    mode = (f.size == 0 && f.priv == NULL) ? 0040755 : 0100644;
  }
  memcpy(u + 24, &mode, 4);

  uint64_t sz = f.size;
  if (mode & 0040000)
    sz = 4096;
  memcpy(u + 48, &sz, 8);

  uint64_t blksize = 4096;
  memcpy(u + 56, &blksize, 8);
  uint64_t blk = (sz + 511) / 512;
  memcpy(u + 64, &blk, 8);

  uint32_t zero = 0;
  memcpy(u + 28, &zero, 4);
  memcpy(u + 32, &zero, 4);

  vfs_close(&f);

  for (int i = 0; i < 144; i++) {
    ((uint8_t *)st)[i] = k_st[i];
  }
  return 0;
}

static int64_t sys_statx(int dfd, const char *path, int flags,
                         unsigned int mask, struct statx *stx) {
  if (!access_ok(stx, sizeof(struct statx)))
    return -EFAULT;
  (void)dfd;
  (void)flags;
  (void)mask;

  char abs_path[256];
  resolve_path(path, abs_path, sizeof(abs_path));
  struct fs_file f;
  if (vfs_open(abs_path, &f) != 0) {
    return -ENOENT;
  }

  struct statx k_stx;
  memset(&k_stx, 0, sizeof(k_stx));

  k_stx.stx_mask = 0x7ff | 0x400;
  k_stx.stx_blksize = 4096;
  k_stx.stx_nlink = 1;
  k_stx.stx_uid = f.uid;
  k_stx.stx_gid = f.gid;
  k_stx.stx_ino = 0;
  uint64_t sz = f.size;
  if (f.mode & 0040000) sz = 4096;
  k_stx.stx_size = sz;
  k_stx.stx_blocks = (sz + 511) / 512;

  uint32_t mode = f.mode;
  if (mode == 0) mode = 0100644;
  k_stx.stx_mode = mode;

  vfs_close(&f);
  uint8_t *src = (uint8_t *)&k_stx;
  uint8_t *dst = (uint8_t *)stx;
  for (size_t i = 0; i < sizeof(struct statx); i++) {
    dst[i] = src[i];
  }

  return 0;
}

static int64_t sys_access(const char *p, int m) {
  (void)m;
  char abs_path[256];
  resolve_path(p, abs_path, sizeof(abs_path));

  struct fs_file f;
  if (vfs_open(abs_path, &f) == 0) {
    vfs_close(&f);
    return 0;
  }

  struct fs_dirent de;
  if (vfs_readdir(abs_path, 0, &de) == 0) {
    return 0;
  }

  return -ENOENT;
}

static int64_t sys_faccessat(int dfd, const char *p, int m, int flags) {
  (void)dfd;
  (void)flags;
  (void)m;
  
  char abs_path[256];
  resolve_path(p, abs_path, sizeof(abs_path));

  if (strcmp(abs_path, "/dev/urandom") == 0 || 
      strcmp(abs_path, "/dev/random") == 0) {
    return 0;
  }

  struct fs_file f;
  if (vfs_open(abs_path, &f) == 0) {
    vfs_close(&f);
    return 0;
  }

  struct fs_dirent de;
  if (vfs_readdir(abs_path, 0, &de) == 0) {
    return 0;
  }

  return -ENOENT;
}

static uint64_t user_brk_base[64];
static uint64_t user_brk_cur[64];
static uint64_t user_mmap_base[64];

static int64_t sys_brk(uint64_t a) {
  int pid = current ? current->pid : 0;
  if (pid >= 64)
    return -ENOMEM;
  if (!user_brk_cur[pid]) {
    user_brk_base[pid] = 0x0000100000000000ULL;
    user_brk_cur[pid] = 0x0000100000000000ULL;
  }
  if (a == 0)
    return user_brk_cur[pid];
  if (a >= user_brk_base[pid]) {
    while (user_brk_cur[pid] < a) {
      uint64_t pg = pmm_alloc();
      if (!pg)
        return user_brk_cur[pid];
      vm_map((uint64_t *)current->cr3, user_brk_cur[pid], pg, 0x07);
      memset((void *)pg, 0, 4096);
      user_brk_cur[pid] += 4096;
    }
  }
  return user_brk_cur[pid];
}

void reset_mm_state(int pid) {
  if (pid >= 0 && pid < 64) {
    user_brk_base[pid] = 0;
    user_brk_cur[pid] = 0;
    user_mmap_base[pid] = 0;
  }
}

static uint8_t *mmap_arena;
static size_t mmap_off = 0;
static size_t mmap_cap = 0;
static void mmap_arena_get(void) {
  if (mmap_arena)
    return;
  uint64_t b = pmm_alloc();
  if (!b)
    return;
  for (int i = 1; i < 4194304 / 4096; i++) {
    if (!pmm_alloc())
      return;
  }
  mmap_arena = (uint8_t *)b;
  mmap_cap = 4194304;
}

struct file_vma {
  int used;
  int pid;
  uint64_t start;
  uint64_t length;
  int fd;
  uint64_t file_offset;
  int prot;
};
#define MAX_VMAS 1024
static struct file_vma vmas[MAX_VMAS];

int handle_file_page_fault(uint64_t addr, uint64_t err_code) {
  if (err_code & 0x1)
    return 0;

  uint64_t page_addr = addr & ~0xFFF;
  int pid = current ? current->pid : 0;

  struct file_vma *vma = NULL;
  for (int i = 0; i < MAX_VMAS; i++) {
    if (vmas[i].used && vmas[i].pid == pid && page_addr >= vmas[i].start &&
        page_addr < (vmas[i].start + vmas[i].length)) {
      vma = &vmas[i];
      break;
    }
  }
  if (!vma)
    return 0;

  uint64_t phys_page = pmm_alloc();
  if (!phys_page)
    return 0;
  memset((void *)phys_page, 0, 4096);

  uint64_t offset_in_vma = page_addr - vma->start;
  uint64_t file_offset = vma->file_offset + offset_in_vma;

  int fd = vma->fd;
  if (fd >= 0 && fd < MAXFD && fdtable[fd].used) {
    struct fs_file *f = &fdtable[fd].f;
    size_t old_pos = f->pos;

    f->pos = file_offset;
    size_t to_read = 4096;
    if (file_offset + to_read > f->size) {
      to_read = f->size - file_offset;
    }

    if (to_read > 0) {
      vfs_read(f, (void *)phys_page, to_read);
    }
    f->pos = old_pos;
  }

  uint64_t flags = 0x05;
  if (vma->prot & 0x2)
    flags |= 0x02;

  vm_map((uint64_t *)current->cr3, page_addr, phys_page, flags);

  __asm__ volatile("invlpg (%0)" ::"r"(page_addr) : "memory");

  return 1;
}

static int64_t sys_mmap(uint64_t a, size_t len, int p, int f, int fd,
                        int64_t o) {
  int pid = current ? current->pid : 0;
  if (pid >= 64)
    return -ENOMEM;
  if (len == 0)
    return -EINVAL;

  if (!user_mmap_base[pid]) {
    user_mmap_base[pid] = 0x0000600000000000ULL;
  }

  (void)p;
  len = (len + 4095) & ~4095UL;

  uint64_t va = user_mmap_base[pid];
  if (va >= 0x0000700000000000ULL) {
    user_mmap_base[pid] = 0x0000600000000000ULL;
    va = user_mmap_base[pid];
  }

  if (fd >= 3 && fd < MAXFD && fdtable[fd].used) {
    int vma_idx = -1;
    for (int i = 0; i < MAX_VMAS; i++) {
      if (!vmas[i].used) {
        vma_idx = i;
        break;
      }
    }
    if (vma_idx == -1)
      return -ENOMEM;

    vmas[vma_idx].used = 1;
    vmas[vma_idx].pid = pid;
    vmas[vma_idx].start = va;
    vmas[vma_idx].length = len;
    vmas[vma_idx].fd = fd;
    vmas[vma_idx].file_offset = o;
    vmas[vma_idx].prot = p;

    for (size_t off = 0; off < len; off += 4096) {
      vm_map((uint64_t *)current->cr3, va + off, 0, 0x06);
    }

    user_mmap_base[pid] += len;
    return va;
  }

#ifdef CONFIG_VM
  if (current && current->cr3) {
    int fixed = (f & 0x10) && a;
    uint64_t va_anon = fixed ? a : user_mmap_base[pid];
    if (va_anon >= 0x0000700000000000ULL) {
      user_mmap_base[pid] = 0x0000600000000000ULL;
      va_anon = user_mmap_base[pid];
    }
    for (size_t off = 0; off < len; off += 4096) {
      uint64_t fr = pmm_alloc();
      if (!fr)
        return -ENOMEM;
      vm_map((uint64_t *)current->cr3, va_anon + off, fr, 0x07);
      memset((void *)fr, 0, 4096);
    }
    if (!fixed) {
      user_mmap_base[pid] += len;
    }
    return va_anon;
  }
#endif
  return -ENOMEM;
}

static int64_t sys_pipe(int pipefd[2]) {
  struct pipe_buf *pb = kmalloc(sizeof(struct pipe_buf));
  if (!pb)
    return -ENOMEM;
  memset(pb, 0, sizeof(*pb));
  pb->refs = 2;
  pb->write_closed = 0;
  pb->read_refs = 1;
  pb->write_refs = 1;
  int rfd = fd_alloc();
  if (rfd < 0) {
    kfree(pb);
    return -EMFILE;
  }
  int wfd = fd_alloc();
  if (wfd < 0) {
    fdtable[rfd].used = 0;
    kfree(pb);
    return -EMFILE;
  }
  fdtable[rfd].used = 1;
  fdtable[rfd].f.priv = pb;
  fdtable[rfd].f.mode = 0020666;
  copy_str(fdtable[rfd].path, "/pipe/read", 128);
  fdtable[wfd].used = 1;
  fdtable[wfd].f.priv = pb;
  fdtable[wfd].f.mode = 0020666;
  copy_str(fdtable[wfd].path, "/pipe/write", 128);
  pipefd[0] = rfd;
  pipefd[1] = wfd;
  return 0;
}

static int64_t sys_munmap(uint64_t a, size_t l) {
  (void)a;
  (void)l;
  return 0;
}
static int64_t sys_mprotect(uint64_t addr, size_t len, int prot) {
  if (!current || !current->cr3) return -EINVAL;
  if (addr & 0xFFF) return -EINVAL;
  if (len == 0) return 0;
  len = (len + 0xFFF) & ~0xFFFULL;
  if (addr >= 0x0000800000000000ULL || addr + len > 0x0000800000000000ULL) {
    return -ENOMEM;
  }
  uint64_t *p4 = (uint64_t *)current->cr3;
  uint64_t end = addr + len;
  for (uint64_t curr = addr; curr < end; curr += 4096) {
    int i4 = (curr >> 39) & 511;
    if (!(p4[i4] & 1)) return -ENOMEM;
    uint64_t *p3 = (uint64_t *)(p4[i4] & 0x000FFFFFFFFFF000ULL);
    int i3 = (curr >> 30) & 511;
    if (!(p3[i3] & 1)) return -ENOMEM;
    if (p3[i3] & 0x80) return -ENOTSUP;
    uint64_t *p2 = (uint64_t *)(p3[i3] & 0x000FFFFFFFFFF000ULL);
    int i2 = (curr >> 21) & 511;
    if (!(p2[i2] & 1)) return -ENOMEM;
    if (p2[i2] & 0x80) return -ENOTSUP;
    uint64_t *p1 = (uint64_t *)(p2[i2] & 0x000FFFFFFFFFF000ULL);
    int i1 = (curr >> 12) & 511;
    if (!(p1[i1] & 1)) return -ENOMEM;
    uint64_t flags = 0x05;
    if (prot & 2) flags |= 0x02;
    uint64_t pa = p1[i1] & 0x000FFFFFFFFFF000ULL;
    p1[i1] = pa | flags;
    __asm__ volatile("invlpg (%0)" ::"r"(curr) : "memory");
  }
  return 0;
}

static int64_t sys_gettimeofday(struct timeval *tv, void *tz) {
  (void)tz;
  if (tv) {
    tv->tv_sec = 0;
    tv->tv_usec = 0;
  }
  return 0;
}
static int64_t sys_clock_gettime(int c, struct timespec *ts) {
  (void)c;
  if (ts) {
    uint64_t t = rdtsc();
    ts->tv_sec = t / 1000000000UL;
    ts->tv_nsec = t % 1000000000UL;
  }
  return 0;
}
static int64_t sys_nanosleep(const struct timespec *r, struct timespec *o) {
  (void)r;
  (void)o;
  return 0;
}

static int64_t sys_utimensat(int dfd, const char *path, const void *times,
                             int flags) {
  (void)dfd;
  (void)path;
  (void)times;
  (void)flags;
  return 0;
}

static int64_t sys_utimes(const char *path, const void *times) {
  (void)path;
  (void)times;
  return 0;
}
static int64_t sys_uname(struct utsname *u) {
  if (!u || !access_ok(u, sizeof(struct utsname)))
    return -EFAULT;
  copy_str(u->sysname, "Copper", 65);
  copy_str(u->nodename, "copper", 65);
  copy_str(u->release, "0.2.0-rc2", 65);
  copy_str(u->version, "#1 SMP Copper", 65);
  copy_str(u->machine, "x86_64", 65);
  copy_str(u->domainname, "(none)", 65);
  return 0;
}
static int64_t sys_readlink(const char *p, char *b, size_t s) {
  if (!access_ok(b, s))
    return -EFAULT;
  char abs_path[256];
  resolve_path(p, abs_path, sizeof(abs_path));
  int ret = vfs_readlink(abs_path, b, s);
  if (ret < 0)
    return ret;
  return ret;
}
static int64_t sys_getrandom(void *b, size_t n, unsigned f) {
  if (!access_ok(b, n))
    return -EFAULT;
  (void)f;
  uint8_t *x = b;
  uint64_t s = rdtsc();
  for (size_t i = 0; i < n; i++) {
    s = s * 6364136223846793005UL + 1442695040888963407UL;
    x[i] = (uint8_t)(s >> 33);
  }
  return n;
}

static int64_t sys_arch_prctl(int c, uint64_t a) {
  if (c == 0x1002) {
    current->fsbase = a;
    __asm__ volatile("wrmsr" ::"c"(0xC0000100), "a"((uint32_t)(a & 0xFFFFFFFF)),
                     "d"((uint32_t)(a >> 32)));
    return 0;
  }
  if (c == 0x1001)
    return 0;
  return 0;
}
static int64_t sys_exit(int c) {
  if (current) {
    cleanup_process_fds(current->pid);
  }
#ifdef CONFIG_DEBUG_SYSCALL
  con_puts("[exit] code=");
  put_u64((uint64_t)(int64_t)c);
  con_puts(" task=");
  con_puts(current ? current->name : "?");
  con_puts("\n");
#endif
#ifdef CONFIG_PROC_EXIT
  do_exit(c);
  return 0;
#else
  (void)c;
  __asm__ volatile("cli;hlt");
  return 0;
#endif
}

struct linux_dirent64 {
  uint64_t d_ino;
  int64_t d_off;
  uint16_t d_reclen;
  uint8_t d_type;
  char d_name[];
};
static int64_t sys_getdents64(int fd, void *d, size_t count) {
  if (fd < 3 || !fdtable[fd].used)
    return -EBADF;
  if (!access_ok(d, count))
    return -EFAULT;
  size_t out = 0;
  struct fs_dirent de;
  while (out + 32 < count) {
    if (vfs_readdir(fdtable[fd].path, fdtable[fd].dir_idx, &de))
      break;
    fdtable[fd].dir_idx++;
    size_t nl = strlen(de.name) + 1;
    size_t rec = (24 + nl + 7) & ~7UL;
    if (out + rec > count)
      break;
    struct linux_dirent64 *e = (struct linux_dirent64 *)((uint8_t *)d + out);
    e->d_ino = fdtable[fd].dir_idx;
    e->d_off = out + rec;
    e->d_reclen = rec;
    uint8_t d_type = 8;
    if (de.is_dir) d_type = 4;
    if (de.mode & 0120000) d_type = 10;
    e->d_type = d_type;
    memcpy(e->d_name, de.name, nl);
    out += rec;
  }
  return out;
}
static int64_t sys_pread(int fd, void *b, size_t n, int64_t o) {
  if (fd < 3 || !fdtable[fd].used)
    return -EBADF;
  struct fs_file *f = &fdtable[fd].f;
  size_t old = f->pos;
  f->pos = o;
  int64_t r = vfs_read(f, b, n);
  f->pos = old;
  return r;
}
static int64_t sys_futex(int *u, int op, int val, int64_t to) {
  (void)to;
  op &= 127;
  if (op == 0)
    return futex_wait((uint64_t *)u, val);
  if (op == 1)
    return futex_wake((uint64_t *)u, val);
  return -ENOSYS;
}

struct fork_ctx {
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
  uint64_t rip;
  uint64_t rsp;
  uint64_t rflags;
  uint64_t cr3;
  uint64_t fsbase;
};

__attribute__((naked, unused)) static void fork_iretq(struct fork_ctx *ctx) {
  __asm__ volatile("mov %rdi, %r15 \n\t"
                   "mov 120(%r15), %r8 \n\t"
                   "mov 128(%r15), %r9 \n\t"
                   "mov 136(%r15), %r10 \n\t"

                   "mov $0x23, %rax \n\t"
                   "push %rax \n\t"
                   "push %r9 \n\t"
                   "push %r10 \n\t"
                   "mov $0x2b, %rax \n\t"
                   "push %rax \n\t"
                   "push %r8 \n\t"

                   "mov 8(%r15), %rbx \n\t"
                   "mov 16(%r15), %rcx \n\t"
                   "mov 24(%r15), %rdx \n\t"
                   "mov 32(%r15), %rsi \n\t"
                   "mov 40(%r15), %rdi \n\t"
                   "mov 48(%r15), %rbp \n\t"
                   "mov 56(%r15), %r8 \n\t"
                   "mov 64(%r15), %r9 \n\t"
                   "mov 72(%r15), %r10 \n\t"
                   "mov 80(%r15), %r11 \n\t"
                   "mov 88(%r15), %r12 \n\t"
                   "mov 96(%r15), %r13 \n\t"
                   "mov 104(%r15), %r14 \n\t"
                   "mov 112(%r15), %r15 \n\t"

                   "xor %rax, %rax \n\t"
                   "cld \n\t"
                   "sti \n\t"
                   "iretq \n\t");
}

static void fork_trampoline(void *arg) {
  struct fork_ctx ctx = *(struct fork_ctx *)arg;
  kfree(arg);
  current->cr3 = ctx.cr3;
  current->fsbase = ctx.fsbase;
  __asm__ volatile("mov %0, %%cr3" ::"r"(ctx.cr3) : "memory");
  __asm__ volatile("wrmsr" ::"c"(0xC0000100),
                   "a"((uint32_t)(ctx.fsbase & 0xFFFFFFFF)),
                   "d"((uint32_t)(ctx.fsbase >> 32))
                   : "memory");
  fork_iretq(&ctx);
  __builtin_unreachable();
}

static int64_t sys_fork(struct pt_regs *r) {
  struct fork_ctx *ctx = kmalloc(sizeof(struct fork_ctx));
  if (!ctx)
    return -ENOMEM;
  ctx->rax = 0;
  ctx->rbx = r->rbx;
  ctx->rcx = r->rcx;
  ctx->rdx = r->rdx;
  ctx->rsi = r->rsi;
  ctx->rdi = r->rdi;
  ctx->rbp = r->rbp;
  ctx->r8 = r->r8;
  ctx->r9 = r->r9;
  ctx->r10 = r->r10;
  ctx->r11 = r->r11;
  ctx->r12 = r->r12;
  ctx->r13 = r->r13;
  ctx->r14 = r->r14;
  ctx->r15 = r->r15;
  ctx->rip = r->saved_rip;
  ctx->rsp = r->saved_rsp;
  if (r->rax == 56 && r->rsi != 0 && r->rsi > 0x10000 &&
      r->rsi < 0x0000800000000000ULL) {
    ctx->rsp = r->rsi;
  }
  ctx->rflags = r->saved_rflags | 0x200;
  ctx->cr3 = (uint64_t)vm_clone_as((uint64_t *)current->cr3);
  if (!ctx->cr3) {
    kfree(ctx);
    return -ENOMEM;
  }
  ctx->fsbase = current->fsbase;
  int rc = kthread_create(fork_trampoline, ctx, "fork");
  if (rc < 0) {
    kfree(ctx);
    return rc;
  }

  int parent_pid = current->pid;
  if (parent_pid >= 0 && parent_pid < 64 && rc >= 0 && rc < 64) {
    memcpy(fdtables[rc], fdtables[parent_pid], sizeof(fdtables[0]));
    for (int i = 0; i < MAXFD; i++) {
      if (fdtables[rc][i].used) {
        inc_priv_refs(&fdtables[rc][i].f);
        if (strcmp(fdtables[rc][i].path, "/pipe/read") == 0) {
          struct pipe_buf *pb = fdtables[rc][i].f.priv;
          if (pb)
            pb->read_refs++;
        } else if (strcmp(fdtables[rc][i].path, "/pipe/write") == 0) {
          struct pipe_buf *pb = fdtables[rc][i].f.priv;
          if (pb)
            pb->write_refs++;
        }
      }
    }
    memcpy(cwd_table[rc], cwd_table[parent_pid], 256);
    user_brk_base[rc] = user_brk_base[parent_pid];
    user_brk_cur[rc] = user_brk_cur[parent_pid];
    user_mmap_base[rc] = user_mmap_base[parent_pid];
  }

  return rc;
}

static int64_t sys_dup2(int oldfd, int newfd) {
  if (oldfd < 0 || oldfd >= MAXFD || !fdtable[oldfd].used)
    return -EBADF;
  if (newfd < 0 || newfd >= MAXFD)
    return -EBADF;
  if (oldfd == newfd)
    return newfd;

  if (fdtable[newfd].used) {
    sys_closefd(newfd);
  }
  fdtable[newfd] = fdtable[oldfd];
  inc_priv_refs(&fdtable[newfd].f);

  if (strcmp(fdtable[newfd].path, "/pipe/read") == 0) {
    struct pipe_buf *pb = fdtable[newfd].f.priv;
    if (pb)
      pb->read_refs++;
  } else if (strcmp(fdtable[newfd].path, "/pipe/write") == 0) {
    struct pipe_buf *pb = fdtable[newfd].f.priv;
    if (pb)
      pb->write_refs++;
  }
  return newfd;
}

static int64_t sys_chdir(const char *path) {
  if (!path)
    return -EFAULT;
  char *cwd = cwd_of();
  if (strcmp(path, ".") == 0) {
    return 0;
  }

  if (strcmp(path, "..") == 0) {
    char *last_slash = NULL;
    for (int i = strlen(cwd) - 1; i >= 0; i--) {
      if (cwd[i] == '/') {
        last_slash = &cwd[i];
        break;
      }
    }
    if (last_slash && last_slash != cwd) {
      *last_slash = '\0';
    } else {
      cwd[0] = '/';
      cwd[1] = '\0';
    }
    return 0;
  }

  char new_cwd[256];
  size_t cwd_len = strlen(cwd);
  size_t path_len = strlen(path);

  if (path[0] == '/') {
    if (path_len >= 256)
      return -ENAMETOOLONG;
    memcpy(new_cwd, path, path_len + 1);
  } else {
    if (cwd_len == 1 && cwd[0] == '/') {
      if (1 + path_len >= 256)
        return -ENAMETOOLONG;
      new_cwd[0] = '/';
      memcpy(new_cwd + 1, path, path_len + 1);
    } else {
      if (cwd_len + 1 + path_len >= 256)
        return -ENAMETOOLONG;
      memcpy(new_cwd, cwd, cwd_len);
      new_cwd[cwd_len] = '/';
      memcpy(new_cwd + cwd_len + 1, path, path_len + 1);
    }
  }
  size_t len = strlen(new_cwd);
  while (len > 1 && new_cwd[len - 1] == '/') {
    new_cwd[--len] = '\0';
  }

  struct fs_file f;
  if (vfs_open(new_cwd, &f) != 0) {
    return -ENOENT;
  }

  if (!(f.mode & 0040000)) {
    vfs_close(&f);
    return -ENOTDIR;
  }
  vfs_close(&f);
  struct fs_dirent de;
  if (vfs_readdir(new_cwd, 0, &de) != 0) {
    return -ENOENT;
  }
  memcpy(cwd, new_cwd, strlen(new_cwd) + 1);
  return 0;
}

static int64_t sys_getcwd(char *buf, size_t size) {
  if (!buf || !access_ok(buf, size))
    return -EFAULT;

  char *cwd = cwd_of();
  size_t len = strlen(cwd);

  if (size < len + 1)
    return -ERANGE;

  memcpy(buf, cwd, len + 1);
  return (int64_t)len;
}

static int64_t sys_ioctl(int fd, unsigned long req, void *arg) {
  if (arg && !access_ok(arg, 128))
    return -EFAULT;
#ifdef CONFIG_TTY
  if (fd >= 0 && fd < MAXFD && fdtable[fd].used && fdtable[fd].f.tty) {
    struct tty *tty = (struct tty *)fdtable[fd].f.tty;
    if (req == 0x5401) {
      if (arg)
        memcpy(arg, &tty->termios, sizeof(struct termios));
      return 0;
    }
    if (req == 0x5402 || req == 0x5403 || req == 0x5404) {
      if (arg)
        memcpy(&tty->termios, arg, sizeof(struct termios));
      return 0;
    }
    if (req == 0x5413) {
      if (arg)
        memcpy(arg, &tty->winsize, sizeof(struct winsize));
      return 0;
    }
    if (req == 0x5414) {
      if (arg)
        memcpy(&tty->winsize, arg, sizeof(struct winsize));
      return 0;
    }
    if (req == 0x540F) {
      if (arg)
        *(int *)arg = tty->pgrp ? tty->pgrp : 1;
      return 0;
    }
    if (req == 0x5410) {
      if (arg)
        tty->pgrp = *(int *)arg;
      return 0;
    }
    if (req == 0x540E)
      return 0;
    return -ENOTTY;
  }
#endif
#ifdef CONFIG_NET
  if (req == 0x8916) {
    struct ifreq_net {
      char name[16];
      struct {
        uint16_t family;
        uint16_t port;
        uint32_t addr;
        uint8_t zero[8];
      } addr;
    } *ifr = arg;

    if (ifr) {
      extern void net_set_ip(uint32_t);
      net_set_ip(ifr->addr.addr);
      return 0;
    }
    return -EFAULT;
  }
#endif
  if (req == 0x5401) {
    if (arg) {
      memset(arg, 0, 120);
      uint32_t *c_lflag = (uint32_t *)((uint8_t *)arg + 12);
      *c_lflag = 0x10A;
    }
    return 0;
  }
  if (req == 0x5402 || req == 0x5403 || req == 0x5404)
    return 0;
  if (req == 0x5413) {
    if (arg)
      memset(arg, 0, 8);
    return 0;
  }
  return -ENOTTY;
}

static int64_t sys_rt_sigaction(int sig, const void *act, void *oact,
                                size_t sz) {
  (void)sig;
  (void)act;
  (void)oact;
  (void)sz;
  return 0;
}
static int64_t sys_rt_sigprocmask(int how, const void *set, void *oldset,
                                  size_t sz) {
  (void)how;
  (void)set;
  (void)oldset;
  (void)sz;
  return 0;
}
static int64_t sys_sigaltstack(const void *ss, void *old_ss) {
  (void)ss;
  (void)old_ss;
  return 0;
}

static int64_t sys_sysinfo(void *si) {
  if (!access_ok(si, 112))
    return -EFAULT;
  memset(si, 0, 112);
  return 0;
}

struct tms {
  uint64_t tms_utime;
  uint64_t tms_stime;
  uint64_t tms_cutime;
  uint64_t tms_cstime;
};

static int64_t sys_times(struct tms *buf) {
  if (buf && access_ok(buf, sizeof(struct tms))) {
    memset(buf, 0, sizeof(struct tms));
  }
  return 0;
}

struct rusage {
  uint64_t ru_utime_tv_sec;
  uint64_t ru_utime_tv_usec;
  uint64_t ru_stime_tv_sec;
  uint64_t ru_stime_tv_usec;
  int64_t ru_maxrss;
  int64_t ru_ixrss;
  int64_t ru_idrss;
  int64_t ru_isrss;
  int64_t ru_minflt;
  int64_t ru_majflt;
  int64_t ru_nswap;
  int64_t ru_inblock;
  int64_t ru_oublock;
  int64_t ru_msgsnd;
  int64_t ru_msgrcv;
  int64_t ru_nsignals;
  int64_t ru_nvcsw;
  int64_t ru_nivcsw;
};

static int64_t sys_getrusage(int who, struct rusage *usage) {
  (void)who;
  if (usage && access_ok(usage, sizeof(struct rusage))) {
    memset(usage, 0, sizeof(struct rusage));
  }
  return 0;
}

int64_t syscall_dispatch(struct pt_regs *r) {
#ifdef CONFIG_DEBUG_SYSCALL
  {
    static int tc = 0;
    if (tc < 200) {
      tc++;
      con_puts("[s] ");
      put_u64(r->rax);
      con_puts(" ");
    }
  }
#endif

  switch (r->rax) {
  case 0:
    return sys_readfd((int)r->rdi, (void *)r->rsi, r->rdx);
  case 1:
    return sys_writefd((int)r->rdi, (const char *)r->rsi, r->rdx);
  case 2:
    return sys_openat(0, (const char *)r->rdi, (int)r->rsi, (int)r->rdx);
  case 3:
    return sys_closefd((int)r->rdi);
  case 4:
    return stat_path((const char *)r->rdi, (void *)r->rsi);
  case 5:
    return sys_fstatfd((int)r->rdi, (void *)r->rsi);
  case 6:
    return stat_path((const char *)r->rdi, (void *)r->rsi);
  case 7: {
    return 1;
  }
  case 23: {
    if (r->rsi > 0 && r->rsi < USER_SPACE_LIMIT) {
        uint64_t *readfds = (uint64_t *)r->rsi;
        *readfds |= 1; 
    }
    return 1;
  }
  case 270: {
    if (r->rsi > 0 && r->rsi < USER_SPACE_LIMIT) {
        uint64_t *readfds = (uint64_t *)r->rsi;
        *readfds |= 1;
    }
    return 1;
  }
  case 271: {
    return 1;
  }
  case 8:
    return sys_lseekfd((int)r->rdi, (long)r->rsi, (int)r->rdx);
  case 17:
    return sys_pread((int)r->rdi, (void *)r->rsi, r->rdx, r->r10);
  case 19: {
    int64_t t = 0;
    for (int i = 0; i < (int)r->rdx; i++)
      t += sys_readfd((int)r->rdi, ((const struct iovec *)r->rsi)[i].iov_base,
                      ((const struct iovec *)r->rsi)[i].iov_len);
    return t;
  }
  case 24:
    yield();
    return 0;
  case 28:
    return 0;
  case 56:
  case 57: {
    int64_t ret = sys_fork(r);
    return ret;
  }
  case 74:
  case 75:
    return 0;
  case 77:
    return 0;
  case 83:
    return sys_mkdir((const char *)r->rdi, (uint32_t)r->rsi);
  case 84:
    return sys_rmdir((const char *)r->rdi);
  case 87:
    return sys_unlink((const char *)r->rdi);
  case 90:
  case 91:
  case 93:
  case 94:
    return 0;
  case 95:
    return 0022;
  case 109:
    return 0;
  case 111:
    return 1;
  case 157:
    return 0;
  case 202:
    return sys_futex((int *)r->rdi, (int)r->rsi, (int)r->rdx, r->r10);
  case 217:
    return sys_getdents64((int)r->rdi, (void *)r->rsi, r->rdx);
  case 229:
    return 0;
  case 235:
    return sys_utimes((const char *)r->rdi, (const void *)r->rsi);
  case 262:
    return stat_path((const char *)r->rsi, (void *)r->rdx);
  case 263:
    return sys_unlinkat((int)r->rdi, (const char *)r->rsi, (int)r->rdx);
  case 269:
    return sys_access((const char *)r->rsi, (int)r->rdx);
  case 280:
    return sys_utimensat((int)r->rdi, (const char *)r->rsi,
                         (const void *)r->rdx, (int)r->r10);
  case 309:
    return 0;
  case 332:
    return sys_statx((int)r->rdi, (const char *)r->rsi, (int)r->rdx,
                     (unsigned int)r->r10, (struct statx *)r->r8);
  case 9:
    return sys_mmap(r->rdi, r->rsi, (int)r->rdx, (int)r->r10, (int)r->r8,
                    r->r9);
  case 10:
    return sys_mprotect(r->rdi, r->rsi, (int)r->rdx);
  case 11:
    return sys_munmap(r->rdi, r->rsi);
  case 12:
    return sys_brk(r->rdi);
  case 13:
    return sys_rt_sigaction((int)r->rdi, (const void *)r->rsi, (void *)r->rdx,
                            (size_t)r->r10);
  case 14:
    return sys_rt_sigprocmask((int)r->rdi, (const void *)r->rsi, (void *)r->rdx,
                              (size_t)r->r10);
  case 15:
    return 0;
  case 16:
    return sys_ioctl((int)r->rdi, r->rsi, (void *)r->rdx);
  case 20:
    return sys_writevfd((int)r->rdi, (const struct iovec *)r->rsi, (int)r->rdx);
  case 21:
    return sys_access((const char *)r->rdi, (int)r->rsi);
  case 22: {
    int fds[2];
    int64_t ret = sys_pipe(fds);
    if (ret == 0) {
      memcpy((void *)r->rdi, fds, 8);
    }
    return ret;
  }
  case 32:
    return sys_dupfd((int)r->rdi);
  case 33:
    return sys_dup2((int)r->rdi, (int)r->rsi);
  case 35:
    return sys_nanosleep((const struct timespec *)r->rdi,
                         (struct timespec *)r->rsi);
  case 39:
    return sys_getpid();
  case 41:
    return sys_socket((int)r->rdi, (int)r->rsi, (int)r->rdx);
  case 42:
    return sys_connect((int)r->rdi, (const void *)r->rsi, (int)r->rdx);
  case 43:
    return sys_accept((int)r->rdi, (void *)r->rsi, (int *)r->rdx);
  case 44:
    return sys_sendto((int)r->rdi, (const void *)r->rsi, (size_t)r->rdx,
                      (int)r->r10, (const void *)r->r8, (int)r->r9);
  case 45:
    return sys_recvfrom((int)r->rdi, (void *)r->rsi, (size_t)r->rdx,
                        (int)r->r10, (void *)r->r8, (int *)r->r9);
  case 48:
    return sys_faccessat((int)r->rdi, (const char *)r->rsi, (int)r->rdx, (int)r->r10);
  case 49:
    return sys_bind((int)r->rdi, (const void *)r->rsi, (int)r->rdx);
  case 50:
    return sys_listen((int)r->rdi, (int)r->rsi);
  case 54:
    return sys_setsockopt((int)r->rdi, (int)r->rsi, (int)r->rdx,
                          (const void *)r->r10, (int)r->r8);
  case 288:
    return sys_accept((int)r->rdi, (void *)r->rsi, (int *)r->rdx);
  case 59: {
    int rc = execve((const char *)r->rdi, (char *const *)r->rsi,
                    (char *const *)r->rdx);
    if (rc == 0) {
      for (;;)
        yield();
    }
    return rc;
  }
  case 60:
    return sys_exit((int)r->rdi);
  case 61:
    return wait4((int)r->rdi, (int *)r->rsi);
  case 62:
    return 0;
  case 63:
    return sys_uname((struct utsname *)r->rdi);
  case 72:
    return sys_fcntl((int)r->rdi, (int)r->rsi, r->rdx);
  case 79:
    return sys_getcwd((char *)r->rdi, r->rsi);
  case 80:
    return sys_chdir((const char *)r->rdi);
  case 89:
    return sys_readlink((const char *)r->rdi, (char *)r->rsi, r->rdx);
  case 96:
    return sys_gettimeofday((struct timeval *)r->rdi, (void *)r->rsi);
  case 97:
    return 0;
  case 98:
    return sys_getrusage((int)r->rdi, (struct rusage *)r->rsi);
  case 99:
    return sys_sysinfo((void *)r->rdi);
  case 100:
    return sys_times((struct tms *)r->rdi);
  case 102:
    return sys_getuid();
  case 104:
    return sys_getgid();
  case 107:
    return sys_geteuid();
  case 108:
    return sys_getegid();
  case 110:
    return sys_getppid();
  case 112:
    return sys_setsid();
  case 118:
    return 0;
  case 120:
    return 0;
  case 121:
    return 0;
  case 131:
    return 0;
  case 158:
    return sys_arch_prctl((int)r->rdi, r->rsi);
  case 165:
    return sys_mount((const char *)r->rdi, (const char *)r->rsi,
                     (const char *)r->rdx);
  case 166:
    return sys_umount((const char *)r->rdi);
  case 186:
    return sys_gettid();
  case 218:
    return current ? current->pid : 1;
  case 221:
    return 0;
  case 228:
    return sys_clock_gettime((int)r->rdi, (struct timespec *)r->rsi);
  case 231:
    return sys_exit((int)r->rdi);
  case 257: {
    int64_t ret =
        sys_openat((int)r->rdi, (const char *)r->rsi, (int)r->rdx, (int)r->r10);
    return ret;
  }
  case 273:
    return 0;
  case 292:
    return sys_dup2((int)r->rdi, (int)r->rsi);
  case 293: {
    int fds[2];
    int64_t ret = sys_pipe(fds);
    if (ret == 0) {
      memcpy((void *)r->rdi, fds, 8);
    }
    return ret;
  }
  case 302:
    return 0;
  case 318:
    return sys_getrandom((void *)r->rdi, r->rsi, (unsigned)r->rdx);
  case 439:
    return sys_faccessat((int)r->rdi, (const char *)r->rsi, (int)r->rdx, (int)r->r10);
  default: {
#ifdef CONFIG_DEBUG_SYSCALL
    static int64_t last = -1;
    if ((int64_t)r->rax != last) {
      last = r->rax;
      con_puts("[sys?] ");
      put_u64(r->rax);
      con_puts("\n");
    }
#endif
    return -ENOSYS;
  }
  }
}

void syscall_init(void) {
  wrmsr(MSR_EFER, rdmsr(MSR_EFER) | EFER_SCE);
  wrmsr(MSR_STAR, ((uint64_t)0x08 << 32) | ((uint64_t)0x18 << 48));
  wrmsr(MSR_LSTAR, (uint64_t)syscall_entry);
  wrmsr(MSR_FMASK, 0x200);
  wrmsr(MSR_KERNEL_GS_BASE, (uint64_t)percpu_data);
}
void syscall_selftest(void) {
  struct pt_regs r;
  memset(&r, 0, sizeof(r));
  const char *m = "hello from Linux ABI syscall\n";
  r.rax = 1;
  r.rdi = 1;
  r.rsi = (uint64_t)m;
  r.rdx = strlen(m);
  syscall_dispatch(&r);
  struct utsname u;
  r.rax = 63;
  r.rdi = (uint64_t)&u;
  syscall_dispatch(&r);
  con_puts("[OK] uname: ");
  con_puts(u.sysname);
  con_puts(" ");
  con_puts(u.machine);
  con_puts("\n");
}
