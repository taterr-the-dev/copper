#include <kernel/blkdev.h>
#include <kernel/console.h>
#include <kernel/string.h>
static struct blkdev *devs[8];
static int ndev;
int blkdev_register(struct blkdev *d) {
  if (ndev < 8) {
    devs[ndev++] = d;
    return 0;
  }
  return -1;
}
struct blkdev *blkdev_by_name(const char *n) {
  for (int i = 0; i < ndev; i++)
    if (!strcmp(devs[i]->name, n))
      return devs[i];
  return 0;
}
int blkdev_count(void) { return ndev; }
struct blkdev *blkdev_get(int i) { return (i >= 0 && i < ndev) ? devs[i] : 0; }

struct part {
  struct blkdev *parent;
  uint64_t off;
};
static struct part parts[8];
static struct blkdev pdevs[8];
static char pnames[8][16];
static int npart;

static int part_read(struct blkdev *d, uint64_t off, void *buf, size_t len) {
  struct part *p = d->priv;
  return p->parent->read(p->parent, p->off + off, buf, len);
}
static int part_write(struct blkdev *d, uint64_t off, const void *buf,
                      size_t len) {
  struct part *p = d->priv;
  return p->parent->write(p->parent, p->off + off, buf, len);
}

void blkdev_scan_partitions(struct blkdev *d) {
  uint8_t mbr[512];
  if (d->read(d, 0, mbr, 512))
    return;
  if (mbr[510] != 0x55 || mbr[511] != 0xAA)
    return;
  for (int i = 0; i < 4; i++) {
    uint8_t *e = mbr + 446 + i * 16;
    uint8_t type = e[4];
    if (type == 0 || type == 0xEE || type == 0x05 || type == 0x0F)
      continue;
    uint32_t start = *(uint32_t *)(e + 8), size = *(uint32_t *)(e + 12);
    if (!start || !size || npart >= 8)
      continue;
    parts[npart].parent = d;
    parts[npart].off = (uint64_t)start * 512;
    int L = 0;
    const char *s = d->name;
    while (*s) {
      pnames[npart][L++] = *s++;
    }
    pnames[npart][L++] = 'p';
    pnames[npart][L++] = '1' + i;
    pnames[npart][L] = 0;
    pdevs[npart].name = pnames[npart];
    pdevs[npart].block_size = 512;
    pdevs[npart].read = part_read;
    pdevs[npart].write = part_write;
    pdevs[npart].priv = &parts[npart];
    pdevs[npart].size = (uint64_t)size * 512;
    blkdev_register(&pdevs[npart]);
    con_puts("[BLK] partition ");
    con_puts(pnames[npart]);
    con_puts("\n");
    npart++;
  }
}
