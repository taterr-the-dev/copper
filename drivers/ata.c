#include <autoconf.h>
#ifdef CONFIG_ATA
#include <kernel/blkdev.h>
#include <kernel/io.h>
#include <kernel/string.h>
#define B 0x1F0
static uint8_t st(void) { return inb(B + 7); }
static int wait_bsy(void) {
  for (int i = 0; i < 2000000; i++)
    if (!(st() & 0x80))
      return 0;
  return -1;
}
static int wait_drq(void) {
  for (int i = 0; i < 2000000; i++) {
    uint8_t s = st();
    if (s & 0x08)
      return 0;
    if (s & 0x01)
      return -1;
  }
  return -1;
}
static void sel(uint64_t lba) {
  wait_bsy();
  outb(B + 6, 0xE0 | ((lba >> 24) & 0x0F));
  outb(B + 2, 1);
  outb(B + 3, lba & 0xFF);
  outb(B + 4, (lba >> 8) & 0xFF);
  outb(B + 5, (lba >> 16) & 0xFF);
  st();
  st();
}
static int rd_sec(uint64_t lba, uint8_t *b) {
  sel(lba);
  outb(B + 7, 0x20);
  if (wait_drq())
    return -1;
  for (int i = 0; i < 256; i++) {
    uint16_t w = inw(B);
    b[i * 2] = w & 0xFF;
    b[i * 2 + 1] = w >> 8;
  }
  return 0;
}
static int wr_sec(uint64_t lba, const uint8_t *b) {
  sel(lba);
  outb(B + 7, 0x30);
  if (wait_drq())
    return -1;
  for (int i = 0; i < 256; i++)
    outw(B, b[i * 2] | (b[i * 2 + 1] << 8));
  return wait_bsy();
}
static int ata_read(struct blkdev *d, uint64_t off, void *buf, size_t len) {
  (void)d;
  uint8_t t[512];
  uint64_t s = off / 512, o = off % 512;
  size_t done = 0;
  uint8_t *out = buf;
  while (done < len) {
    if (rd_sec(s, t))
      return -1;
    size_t c = 512 - o;
    if (c > len - done)
      c = len - done;
    memcpy(out + done, t + o, c);
    done += c;
    o = 0;
    s++;
  }
  return 0;
}
static int ata_write(struct blkdev *d, uint64_t off, const void *buf,
                     size_t len) {
  (void)d;
  uint8_t t[512];
  uint64_t s = off / 512, o = off % 512;
  size_t done = 0;
  const uint8_t *in = buf;
  while (done < len) {
    if (rd_sec(s, t))
      return -1;
    size_t c = 512 - o;
    if (c > len - done)
      c = len - done;
    memcpy(t + o, in + done, c);
    if (wr_sec(s, t))
      return -1;
    done += c;
    o = 0;
    s++;
  }
  return 0;
}
static struct blkdev ata = {
    .name = "ata0", .block_size = 512, .read = ata_read, .write = ata_write};
int ata_init(void) {
  wait_bsy();
  outb(B + 6, 0xE0);
  outb(B + 2, 0);
  outb(B + 7, 0xEC);
  uint8_t s = st();
  if (s == 0)
    return -1;
  if (wait_drq())
    return -1;
  uint16_t id[256];
  for (int i = 0; i < 256; i++)
    id[i] = inw(B);
  uint32_t secs = id[60] | ((uint32_t)id[61] << 16);
  if (!secs)
    return -1;
  ata.size = (uint64_t)secs * 512;
  return blkdev_register(&ata);
}
#endif
