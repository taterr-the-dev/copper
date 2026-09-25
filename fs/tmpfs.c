#include <kernel/fs.h>
#include <kernel/kmalloc.h>
#include <kernel/string.h>

#define MAXT 32
struct tfile {
  char name[64];
  uint8_t *data;
  size_t size, cap;
  int used;
  int is_dir;
};
static struct tfile tfs[MAXT];

static const char *tbase(const char *p) {
  if (p[0] == '/' && p[1] == 't' && p[2] == 'm' && p[3] == 'p' && p[4] == '/')
    return p + 5;
  return p[0] == '/' ? p + 1 : p;
}

static int tfind(const char *n) {
  for (int i = 0; i < MAXT; i++) {
    if (tfs[i].used && !strcmp(tfs[i].name, n))
      return i;
  }
  return -1;
}

static int talloc(void) {
  for (int i = 0; i < MAXT; i++) {
    if (!tfs[i].used)
      return i;
  }
  return -1;
}

static int tmp_open(void *sb, const char *path, struct fs_file *f) {
  (void)sb;
  int i = tfind(tbase(path));
  if (i < 0)
    return -1;

  f->priv = (void *)(uint64_t)i;
  f->size = tfs[i].size;
  f->pos = 0;

  f->mode = tfs[i].is_dir ? 040755 : 0100644;
  f->uid = 0;
  f->gid = 0;

  return 0;
}

static int64_t tmp_read(struct fs_file *f, void *out, size_t len) {
  int i = (int)(uint64_t)f->priv;
  uint8_t *o = out;
  size_t d = 0;
  while (d < len && f->pos < tfs[i].size) {
    o[d++] = tfs[i].data[f->pos++];
  }
  return d;
}

static int64_t tmp_write(struct fs_file *f, const void *in, size_t len) {
  int i = (int)(uint64_t)f->priv;
  const uint8_t *s = in;
  size_t need = f->pos + len;
  if (need > tfs[i].cap) {
    size_t nc = tfs[i].cap ? tfs[i].cap * 2 : 256;
    while (nc < need)
      nc *= 2;
    uint8_t *nd = kmalloc(nc);
    if (!nd)
      return -1;
    if (tfs[i].data) {
      memcpy(nd, tfs[i].data, tfs[i].size);
      kfree(tfs[i].data);
    }
    tfs[i].data = nd;
    tfs[i].cap = nc;
  }
  memcpy(tfs[i].data + f->pos, s, len);
  f->pos += len;
  if (f->pos > tfs[i].size)
    tfs[i].size = f->pos;
  f->size = tfs[i].size;
  return len;
}

static int tmp_create(void *sb, const char *path) {
  (void)sb;
  const char *n = tbase(path);
  if (tfind(n) >= 0)
    return 0;
  int i = talloc();
  if (i < 0)
    return -1;
  tfs[i].used = 1;
  tfs[i].is_dir = 0;
  tfs[i].size = 0;
  tfs[i].cap = 0;
  tfs[i].data = 0;
  strcpy(tfs[i].name, n);
  return 0;
}

int tmp_unlink_path(const char *path) {
  int i = tfind(tbase(path));
  if (i < 0)
    return -1;
  if (tfs[i].is_dir)
    return -1; // TODO use rmdir for dirs im too bored to do it now
  if (tfs[i].data)
    kfree(tfs[i].data);
  tfs[i].used = 0;
  tfs[i].data = 0;
  tfs[i].size = tfs[i].cap = 0;
  return 0;
}

static int tmp_mkdir(void *sb, const char *path, uint32_t mode) {
  (void)sb;
  (void)mode;
  if (strcmp(path, "/tmp") == 0)
    return -1;

  const char *n = tbase(path);
  if (tfind(n) >= 0)
    return -1;

  int i = talloc();
  if (i < 0)
    return -1;

  tfs[i].used = 1;
  tfs[i].is_dir = 1;
  tfs[i].size = 0;
  tfs[i].cap = 0;
  tfs[i].data = 0;
  strcpy(tfs[i].name, n);
  return 0;
}

static int tmp_rmdir(void *sb, const char *path) {
  (void)sb;
  if (strcmp(path, "/tmp") == 0)
    return -1;

  const char *n = tbase(path);
  int i = tfind(n);
  if (i < 0)
    return -1;
  if (!tfs[i].is_dir)
    return -1;

  size_t nlen = strlen(n);
  for (int j = 0; j < MAXT; j++) {
    if (tfs[j].used && j != i) {
      if (strncmp(tfs[j].name, n, nlen) == 0 && tfs[j].name[nlen] == '/') {
        return -1;
      }
    }
  }

  tfs[i].used = 0;
  if (tfs[i].data)
    kfree(tfs[i].data);
  tfs[i].data = 0;
  tfs[i].size = tfs[i].cap = 0;
  return 0;
}

static int tmp_readdir(void *sb, const char *path, int idx,
                       struct fs_dirent *out) {
  (void)sb;
  const char *prefix = "";
  size_t plen = 0;

  if (strcmp(path, "/tmp") == 0 || strcmp(path, "/") == 0) {
    prefix = "";
    plen = 0;
  } else if (strncmp(path, "/tmp/", 5) == 0) {
    prefix = path + 5;
    plen = strlen(prefix);
  } else {
    return -1;
  }

  int c = -1;
  for (int i = 0; i < MAXT; i++) {
    if (!tfs[i].used)
      continue;

    if (plen == 0) {
      if (strchr(tfs[i].name, '/') != NULL)
        continue;
    } else {
      if (strncmp(tfs[i].name, prefix, plen) != 0)
        continue;
      if (tfs[i].name[plen] != '/')
        continue;
      if (strchr(tfs[i].name + plen + 1, '/') != NULL)
        continue;
    }

    c++;
    if (c == idx) {
      const char *final_name = tfs[i].name;
      if (plen > 0) {
        final_name = tfs[i].name + plen + 1;
      }
      strcpy(out->name, final_name);
      out->is_dir = tfs[i].is_dir;
      out->size = tfs[i].size;
      out->mode = tfs[i].is_dir ? 0755 : 0644;
      out->uid = 0;
      out->gid = 0;
      return 0;
    }
  }
  return -1;
}

static int tmp_mount(struct blkdev *d, void **sbp) {
  (void)d;
  *sbp = 0;
  return 0;
}

struct fs_ops tmp_fs = {.name = "tmpfs",
                        .mount = tmp_mount,
                        .open = tmp_open,
                        .read = tmp_read,
                        .write = tmp_write,
                        .create = tmp_create,
                        .readdir = tmp_readdir,
                        .mkdir = tmp_mkdir,
                        .rmdir = tmp_rmdir};
