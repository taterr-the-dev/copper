#include <autoconf.h>
#include <kernel/binfmt.h>
#include <kernel/kmalloc.h>
#include <kernel/string.h>
#ifdef CONFIG_BINFMT_ELF
#include <kernel/elf.h>
#endif
static struct binfmt *fmts[4];
static int nfmt;
void binfmt_register(struct binfmt *b) {
  if (nfmt < 4)
    fmts[nfmt++] = b;
}
static int read_all(struct fs_file *f, uint8_t **out, size_t *sz) {
  size_t s = f->size;
  uint8_t *b = kmalloc(s + 1);
  if (!b)
    return -1;
  size_t g = 0;
  while (g < s) {
    int64_t r = vfs_read(f, b + g, s - g);
    if (r <= 0)
      break;
    g += r;
  }
  b[g] = 0;
  *out = b;
  *sz = g;
  return 0;
}
#ifdef CONFIG_BINFMT_ELF
static int elf_probe(struct fs_file *f) {
  uint8_t m[4];
  size_t p = f->pos;
  f->pos = 0;
  int64_t r = vfs_read(f, m, 4);
  f->pos = p;
  return (r == 4 && m[0] == 0x7f && m[1] == 'E' && m[2] == 'L' && m[3] == 'F');
}
static int elf_loadf(struct fs_file *f, struct exec_info *ei) {
  uint8_t *b;
  size_t s;
  if (read_all(f, &b, &s))
    return -1;
  uint64_t e = 0;
  int rc = elf_load(b, s, &e);
  ei->entry = e;
  ei->is_script = 0;
  kfree(b);
  return rc;
}
static struct binfmt elf_fmt = {
    .name = "elf", .probe = elf_probe, .load = elf_loadf};
#endif
#ifdef CONFIG_BINFMT_SHEBANG
static int sh_probe(struct fs_file *f) {
  uint8_t m[2];
  size_t p = f->pos;
  f->pos = 0;
  int64_t r = vfs_read(f, m, 2);
  f->pos = p;
  return (r == 2 && m[0] == '#' && m[1] == '!');
}
static int sh_load(struct fs_file *f, struct exec_info *ei) {
  uint8_t *b;
  size_t s;
  if (read_all(f, &b, &s))
    return -1;
  size_t i = 2;
  while (i < s && b[i] == ' ')
    i++;
  size_t o = 0;
  while (i < s && b[i] != '\n' && b[i] != ' ' && o < 127)
    ei->interpreter[o++] = b[i++];
  ei->interpreter[o] = 0;
  while (i < s && b[i] == ' ')
    i++;
  o = 0;
  while (i < s && b[i] != '\n' && o < 63)
    ei->arg[o++] = b[i++];
  ei->arg[o] = 0;
  ei->is_script = 1;
  kfree(b);
  return 0;
}
static struct binfmt sh_fmt = {
    .name = "shebang", .probe = sh_probe, .load = sh_load};
#endif
void binfmt_init(void) {
#ifdef CONFIG_BINFMT_ELF
  binfmt_register(&elf_fmt);
#endif
#ifdef CONFIG_BINFMT_SHEBANG
  binfmt_register(&sh_fmt);
#endif
}
int exec_load(const char *path, struct exec_info *ei) {
  struct fs_file f;
  if (vfs_open(path, &f))
    return -1;
  for (int i = 0; i < nfmt; i++)
    if (fmts[i]->probe(&f)) {
      int rc = fmts[i]->load(&f, ei);
      vfs_close(&f);
      return rc;
    }
  vfs_close(&f);
  return -1;
}
