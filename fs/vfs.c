#include <autoconf.h>
#include <kernel/blkdev.h>
#include <kernel/console.h>
#include <kernel/fs.h>
#include <kernel/proc.h>
#include <kernel/string.h>
#include <kernel/syscall.h>

#define MAX_MOUNTS 16

static struct fs_ops *list[8];
static int cnt;
static struct mount_point mounts[MAX_MOUNTS];
static struct mount_point *root_mount = NULL;

extern struct fs_ops proc_fs, tmp_fs, dev_fs;

#ifdef CONFIG_FS_EXT2
extern struct fs_ops ext2_fs;
#endif

#ifdef CONFIG_FS_EXT3
extern struct fs_ops ext3_fs;
#endif

#ifdef CONFIG_FS_EXT4
extern struct fs_ops ext4_fs;
#endif

#ifdef CONFIG_FS_FAT12
extern struct fs_ops fat12_fs;
#endif

#ifdef CONFIG_FS_FAT16
extern struct fs_ops fat16_fs;
#endif

#ifdef CONFIG_FS_FAT32
extern struct fs_ops fat32_fs;
#endif

void fs_register(struct fs_ops *o) {
  if (cnt < 8)
    list[cnt++] = o;
}

static int istmp(const char *p) {
  return p[0] == '/' && p[1] == 't' && p[2] == 'm' && p[3] == 'p' &&
         (p[4] == '/' || p[4] == 0);
}

static int isproc(const char *p) {
  return p[0] == '/' && p[1] == 'p' && p[2] == 'r' && p[3] == 'o' &&
         p[4] == 'c' && (p[5] == '/' || p[5] == 0);
}

void fs_init(void) {
  memset(mounts, 0, sizeof(mounts));
  void *dev_sb = NULL;
  if (dev_fs.mount(NULL, &dev_sb) == 0) {
    vfs_mount_at("/dev", &dev_fs, dev_sb);
  }
#ifdef CONFIG_FS_EXT4
  fs_register(&ext4_fs);
#endif

#ifdef CONFIG_FS_EXT3
  fs_register(&ext3_fs);
#endif

#ifdef CONFIG_FS_EXT2
  fs_register(&ext2_fs);
#endif

#ifdef CONFIG_FS_FAT32
  fs_register(&fat32_fs);
#endif

#ifdef CONFIG_FS_FAT16
  fs_register(&fat16_fs);
#endif

#ifdef CONFIG_FS_FAT12
  fs_register(&fat12_fs);
#endif
}

static struct mount_point *find_mount(const char *path) {
  struct mount_point *best = NULL;
  int best_len = 0;

  for (int i = 0; i < MAX_MOUNTS; i++) {
    if (!mounts[i].in_use)
      continue;

    int mlen = strlen(mounts[i].path);
    if (strncmp(path, mounts[i].path, mlen) == 0) {
      if (path[mlen] == '/' || path[mlen] == '\0' || mlen == 1) {
        if (mlen > best_len) {
          best = &mounts[i];
          best_len = mlen;
        }
      }
    }
  }

  return best;
}

static const char *get_relative_path(const char *path, struct mount_point *mp) {
  int mlen = strlen(mp->path);
  if (mlen == 1)
    return path;
  if (path[mlen] == '\0')
    return "/";
  return path + mlen;
}

int vfs_mount_at(const char *mount_path, struct fs_ops *ops, void *sb) {
  int slot = -1;
  for (int i = 0; i < MAX_MOUNTS; i++) {
    if (!mounts[i].in_use) {
      slot = i;
      break;
    }
  }
  if (slot < 0)
    return -1;

  strncpy(mounts[slot].path, mount_path, 255);
  mounts[slot].path[255] = '\0';
  mounts[slot].ops = ops;
  mounts[slot].sb = sb;
  mounts[slot].in_use = 1;

  if (strcmp(mount_path, "/") == 0) {
    root_mount = &mounts[slot];
  }

  return 0;
}

int vfs_umount(const char *mount_path) {
  for (int i = 0; i < MAX_MOUNTS; i++) {
    if (mounts[i].in_use && strcmp(mounts[i].path, mount_path) == 0) {
      mounts[i].in_use = 0;
      if (root_mount == &mounts[i])
        root_mount = NULL;
      return 0;
    }
  }
  return -1;
}

int vfs_get_mount(int idx, char *path, int path_sz, char *fsname,
                  int fsname_sz) {
  if (idx < 0 || idx >= MAX_MOUNTS)
    return -1;
  if (!mounts[idx].in_use)
    return -1;
  strncpy(path, mounts[idx].path, path_sz);
  path[path_sz - 1] = '\0';
  strncpy(fsname, mounts[idx].ops->name, fsname_sz);
  fsname[fsname_sz - 1] = '\0';
  return 0;
}

int vfs_mount(struct blkdev *d) {
  for (int i = 0; i < cnt; i++) {
    void *sb = NULL;
    if (list[i]->mount && list[i]->mount(d, &sb) == 0) {
      return vfs_mount_at("/", list[i], sb);
    }
  }
  return -1;
}

int fs_try_mount(struct blkdev *d, struct fs_ops **out_ops, void **out_sb) {
  for (int i = 0; i < cnt; i++) {
    void *sb = NULL;
    if (list[i]->mount && list[i]->mount(d, &sb) == 0) {
      *out_ops = list[i];
      *out_sb = sb;
      return 0;
    }
  }
  return -1;
}

const char *vfs_name(void) {
  return root_mount ? root_mount->ops->name : "none";
}

int vfs_open(const char *p, struct fs_file *f) {
  if (istmp(p)) {
    f->ops = &tmp_fs;
    f->sb = 0;
    f->pos = 0;
    f->mode = 0040777;
    f->uid = 0;
    f->gid = 0;
    f->size = 4096;

    if (strcmp(p, "/tmp") == 0 || strcmp(p, "/tmp/") == 0) {
      return 0;
    }
    return tmp_fs.open(0, p, f);
  }
  if (isproc(p)) {
    f->ops = &proc_fs;
    f->sb = 0;
    f->pos = 0;
    f->mode = 0040555;
    f->uid = 0;
    f->gid = 0;
    f->size = 4096;

    if (strcmp(p, "/proc") == 0 || strcmp(p, "/proc/") == 0) {
      return 0;
    }
    return proc_fs.open(0, p, f);
  }

  struct mount_point *mp = find_mount(p);
  if (!mp)
    return -ENOENT;

  const char *rel_path = get_relative_path(p, mp);
  f->ops = mp->ops;
  f->sb = mp->sb;
  f->pos = 0;

  if (strcmp(rel_path, "/") == 0 || strcmp(rel_path, "") == 0) {
    f->mode = 0040755;
    f->uid = 0;
    f->gid = 0;
    f->size = 4096;
    f->priv = NULL;
    mp->ops->open(mp->sb, rel_path, f);
    return 0;
  }

  int ret = mp->ops->open(mp->sb, rel_path, f);
  if (ret == 0) {
    if (f->mode == 0)
      f->mode = 0100644;
    if (f->uid != 0)
      f->uid = 0;
    if (f->gid != 0)
      f->gid = 0;
  }
  return ret;
}

int vfs_create(const char *p) {
  if (istmp(p))
    return tmp_fs.create(0, p);
  if (isproc(p))
    return -1;

  struct mount_point *mp = find_mount(p);
  if (!mp)
    return -ENOENT;
  if (!mp->ops->create)
    return -ENOSYS;

  const char *rel_path = get_relative_path(p, mp);
  return mp->ops->create(mp->sb, rel_path);
}

int64_t vfs_read(struct fs_file *f, void *out, size_t len) {
  if (!f->ops || !f->ops->read)
    return -1;
  if (!vfs_check_perm(f, 4))
    return -1;

  return f->ops->read(f, out, len);
}

int64_t vfs_write(struct fs_file *f, const void *in, size_t len) {
  if (!f->ops || !f->ops->write) {
    return -EPERM;
  }

  if (!vfs_check_perm(f, 2)) {
    return -EPERM;
  }

  int64_t ret = f->ops->write(f, in, len);
  if (ret < 0) {
  }

  return ret;
}

int vfs_readdir(const char *p, int i, struct fs_dirent *d) {
  if (istmp(p))
    return tmp_fs.readdir(0, p, i, d);
  if (isproc(p))
    return proc_fs.readdir(0, p, i, d);

  struct mount_point *mp = find_mount(p);
  if (!mp)
    return -1;

  const char *rel_path = get_relative_path(p, mp);
  int ret = mp->ops->readdir(mp->sb, rel_path, i, d);

  if (ret == 0) {
    if (d->mode == 0)
      d->mode = d->is_dir ? 0755 : 0644;
    if (d->uid == 0)
      d->uid = 0;
    if (d->gid == 0)
      d->gid = 0;
  }

  return ret;
}

int vfs_close(struct fs_file *f) {
  if (f->ops && f->ops->close) {
    f->ops->close(f);
  }
  return 0;
}

extern int tmp_unlink_path(const char *);
int vfs_unlink(const char *p) {
  if (istmp(p))
    return tmp_unlink_path(p);
  if (isproc(p))
    return -1;

  struct mount_point *mp = find_mount(p);
  if (!mp)
    return -ENOENT;
  if (!mp->ops->unlink)
    return -ENOSYS;

  const char *rel_path = get_relative_path(p, mp);
  return mp->ops->unlink(mp->sb, rel_path);
}

int vfs_mkdir(const char *p, uint32_t mode) {
  if (istmp(p))
    return tmp_fs.mkdir(0, p, mode);
  if (isproc(p))
    return -1;

  struct mount_point *mp = find_mount(p);
  if (!mp)
    return -1;
  if (!mp->ops->mkdir)
    return -1;

  const char *rel_path = get_relative_path(p, mp);
  return mp->ops->mkdir(mp->sb, rel_path, mode);
}

int vfs_rmdir(const char *p) {
  if (istmp(p))
    return tmp_fs.rmdir(0, p);
  if (isproc(p))
    return -1;

  struct mount_point *mp = find_mount(p);
  if (!mp)
    return -1;
  if (!mp->ops->rmdir)
    return -1;

  const char *rel_path = get_relative_path(p, mp);
  return mp->ops->rmdir(mp->sb, rel_path);
}

int vfs_readlink(const char *path, char *buf, size_t bufsz) {
  struct mount_point *mp = find_mount(path);
  if (!mp)
    return -ENOENT;
  if (!mp->ops->readlink)
    return -ENOSYS;

  const char *rel_path = get_relative_path(path, mp);
  return mp->ops->readlink(mp->sb, rel_path, buf, bufsz);
}

int vfs_check_perm(struct fs_file *f, int access_mode) {
  (void)f;
  (void)access_mode;
  // TODO implement proper permission checking based on current user
  return 1;
}
