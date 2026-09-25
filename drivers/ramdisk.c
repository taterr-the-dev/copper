#include <kernel/blkdev.h>
#include <kernel/string.h>
static uint8_t *base;
static uint64_t rsize;
static int rd_read(struct blkdev *d, uint64_t o, void *b, size_t l) {
  (void)d;
  if (o + l > rsize)
    return -1;
  memcpy(b, base + o, l);
  return 0;
}
static int rd_write(struct blkdev *d, uint64_t o, const void *b, size_t l) {
  (void)d;
  if (o + l > rsize)
    return -1;
  memcpy(base + o, b, l);
  return 0;
}
static struct blkdev rd = {
    .name = "rd0", .read = rd_read, .write = rd_write, .block_size = 512};
int ramdisk_attach(uint64_t b, uint64_t sz) {
  base = (uint8_t *)b;
  rsize = sz;
  rd.size = sz;
  return blkdev_register(&rd);
}
