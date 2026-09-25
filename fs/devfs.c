#include <kernel/blkdev.h>
#include <kernel/console.h>
#include <kernel/fs.h>
#include <kernel/kmalloc.h>
#include <kernel/string.h>
extern void put_u64(uint64_t v);

struct devfs_entry {
  int refs;
  char name[32];
  int is_dir;
  int dev_type;
  int dev_major;
  int dev_minor;
};

static struct devfs_entry dev_entries[] = {
    {1, ".", 1, 0, 0, 0},       {1, "..", 1, 0, 0, 0},
    {1, "console", 0, 1, 5, 1}, {1, "tty", 0, 1, 5, 0},
    {1, "ptmx", 0, 1, 5, 2},    {1, "stdin", 0, 1, 5, 0},
    {1, "stdout", 0, 1, 5, 1},  {1, "stderr", 0, 1, 5, 2},
    {1, "null", 0, 1, 1, 3},    {1, "zero", 0, 1, 1, 5},
};

#define NUM_STATIC_DEV (sizeof(dev_entries) / sizeof(dev_entries[0]))

static int devfs_mount(struct blkdev *d, void **sbp) {
  (void)d;
  *sbp = (void *)1;
  return 0;
}

static int devfs_open(void *sbp, const char *path, struct fs_file *f) {
  (void)sbp;
  if (strcmp(path, ".") == 0 || strcmp(path, "/") == 0 ||
      strcmp(path, "/.") == 0) {
    f->priv = NULL;
    f->size = 0;
    f->mode = 0040755;
    f->uid = 0;
    f->gid = 0;
    return 0;
  }
  const char *name = path;
  if (strncmp(path, "/dev/", 5) == 0) {
    name = path + 5;
  } else if (path[0] == '/') {
    name = path + 1;
  }
  if (strcmp(name, ".") == 0) {
    f->priv = NULL;
    f->size = 0;
    f->mode = 0755;
    return 0;
  }

  for (size_t i = 0; i < NUM_STATIC_DEV; i++) {
    if (strcmp(dev_entries[i].name, name) == 0) {
      struct devfs_entry *entry = kmalloc(sizeof(struct devfs_entry));
      if (!entry)
        return -1;
      *entry = dev_entries[i];
      entry->refs = 1;
      f->priv = entry;
      f->size = 0;
      f->mode =
          entry->is_dir ? 0040755 : (entry->dev_type == 1 ? 0020666 : 0100666);
      f->uid = 0;
      f->gid = 0;
      return 0;
    }
  }

  struct blkdev *bdev = blkdev_by_name(name);
  if (bdev) {
    f->priv = bdev;
    f->size = bdev->size;
    f->mode = 0060660;
    f->uid = 0;
    f->gid = 0;
    return 0;
  }

  return -1;
}

static int64_t devfs_read(struct fs_file *f, void *buf, size_t len) {
  if ((f->mode & 0170000) == 0060000) {
    struct blkdev *bdev = (struct blkdev *)f->priv;
    if (bdev && bdev->read) {
      if (bdev->read(bdev, f->pos, buf, len) == 0) {
        f->pos += len;
        return len;
      }
      return -1;
    }
  }

  struct devfs_entry *entry = f->priv;
  if (!entry)
    return 0;
  if (entry->dev_type == 1) {
    if (entry->dev_major == 1 && entry->dev_minor == 3) {
      return 0;
    }
    if (entry->dev_major == 1 && entry->dev_minor == 5) {
      memset(buf, 0, len);
      return len;
    }
    if (entry->dev_major == 5 && entry->dev_minor == 0) {
      extern int kbd_read(char *, int);
      return kbd_read((char *)buf, (int)len);
    }
  }

  return 0;
}

static int64_t devfs_write(struct fs_file *f, const void *buf, size_t len) {
  if ((f->mode & 0170000) == 0060000) {
    struct blkdev *bdev = (struct blkdev *)f->priv;
    if (bdev && bdev->write) {
      if (bdev->write(bdev, f->pos, buf, len) == 0) {
        f->pos += len;
        return len;
      }
      return -1;
    }
  }

  struct devfs_entry *entry = f->priv;
  const char *s = (const char *)buf;
  if (!entry || (entry->dev_type == 1 && entry->dev_major == 5)) {
    for (size_t i = 0; i < len; i++) {
      con_putc(s[i]);
    }
    return len;
  }
  if (entry->dev_type == 1 && entry->dev_major == 1 && entry->dev_minor == 3) {
    return len;
  }
  return -1;
}

static int devfs_create(void *sbp, const char *path) {
  (void)sbp;
  (void)path;
  return -1;
}

static int devfs_readdir(void *sbp, const char *path, int idx,
                         struct fs_dirent *out) {
  (void)sbp;
  if (strcmp(path, "/") != 0 && strcmp(path, "/dev") != 0 &&
      strcmp(path, "/.") != 0) {
    return -1;
  }

  if (idx < (int)NUM_STATIC_DEV) {
    strncpy(out->name, dev_entries[idx].name, 255);
    out->name[255] = '\0';
    out->is_dir = dev_entries[idx].is_dir;
    out->size = 0;
    out->mode = dev_entries[idx].is_dir ? 0040755 : 0666;
    out->uid = 0;
    out->gid = 0;
    return 0;
  }

  int blk_idx = idx - NUM_STATIC_DEV;
  if (blk_idx >= blkdev_count())
    return -1;

  struct blkdev *bdev = blkdev_get(blk_idx);
  strncpy(out->name, bdev->name, 255);
  out->name[255] = '\0';
  out->is_dir = 0;
  out->size = 0;
  out->mode = 0660;
  out->uid = 0;
  out->gid = 0;
  return 0;
}

int devfs_close(struct fs_file *f) {
  if (f->priv && (f->mode & 0170000) != 0060000) {
    struct devfs_entry *entry = (struct devfs_entry *)f->priv;
    entry->refs--;
    if (entry->refs == 0) {
      kfree(f->priv);
    }
  }
  f->priv = NULL;
  return 0;
}

struct fs_ops dev_fs = {
    .name = "devfs",
    .mount = devfs_mount,
    .open = devfs_open,
    .close = devfs_close,
    .read = devfs_read,
    .write = devfs_write,
    .create = devfs_create,
    .readdir = devfs_readdir,
};
