#include <autoconf.h>
#ifdef CONFIG_ATA
#include <kernel/blkdev.h>
#include <kernel/io.h>
#include <kernel/string.h>
#include <kernel/console.h>

#define MAX_ATA_DRIVES 4

struct ata_priv {
    uint16_t base;
    uint8_t drive;
};

static struct ata_priv ata_privs[MAX_ATA_DRIVES];
static struct blkdev ata_devs[MAX_ATA_DRIVES];
static char ata_names[MAX_ATA_DRIVES][8];
static int ata_count = 0;

static uint8_t ata_st(uint16_t base) { return inb(base + 7); }

static int ata_wait_bsy(uint16_t base) {
    for (int i = 0; i < 2000000; i++)
        if (!(ata_st(base) & 0x80)) return 0;
    return -1;
}

static int ata_wait_drq(uint16_t base) {
    for (int i = 0; i < 2000000; i++) {
        uint8_t s = ata_st(base);
        if (s & 0x08) return 0;
        if (s & 0x01) return -1;
    }
    return -1;
}

static void ata_sel(struct ata_priv *p, uint64_t lba) {
    ata_wait_bsy(p->base);
    outb(p->base + 6, p->drive | ((lba >> 24) & 0x0F));
    outb(p->base + 2, 1);
    outb(p->base + 3, lba & 0xFF);
    outb(p->base + 4, (lba >> 8) & 0xFF);
    outb(p->base + 5, (lba >> 16) & 0xFF);
    ata_st(p->base); ata_st(p->base);
}

static int ata_rd_sec(struct ata_priv *p, uint64_t lba, uint8_t *b) {
    ata_sel(p, lba);
    outb(p->base + 7, 0x20);
    if (ata_wait_drq(p->base)) return -1;
    for (int i = 0; i < 256; i++) {
        uint16_t w = inw(p->base);
        b[i * 2] = w & 0xFF;
        b[i * 2 + 1] = w >> 8;
    }
    return 0;
}

static int ata_wr_sec(struct ata_priv *p, uint64_t lba, const uint8_t *b) {
    ata_sel(p, lba);
    outb(p->base + 7, 0x30);
    if (ata_wait_drq(p->base)) return -1;
    for (int i = 0; i < 256; i++)
        outw(p->base, b[i * 2] | (b[i * 2 + 1] << 8));
    return ata_wait_bsy(p->base);
}

static int ata_read(struct blkdev *d, uint64_t off, void *buf, size_t len) {
    struct ata_priv *p = d->priv;
    uint8_t t[512];
    uint64_t s = off / 512, o = off % 512;
    size_t done = 0;
    uint8_t *out = buf;
    while (done < len) {
        if (ata_rd_sec(p, s, t)) return -1;
        size_t c = 512 - o;
        if (c > len - done) c = len - done;
        memcpy(out + done, t + o, c);
        done += c; o = 0; s++;
    }
    return 0;
}

static int ata_write(struct blkdev *d, uint64_t off, const void *buf, size_t len) {
    struct ata_priv *p = d->priv;
    uint8_t t[512];
    uint64_t s = off / 512, o = off % 512;
    size_t done = 0;
    const uint8_t *in = buf;
    while (done < len) {
        if (ata_rd_sec(p, s, t)) return -1;
        size_t c = 512 - o;
        if (c > len - done) c = len - done;
        memcpy(t + o, in + done, c);
        if (ata_wr_sec(p, s, t)) return -1;
        done += c; o = 0; s++;
    }
    return 0;
}

static int ata_probe(uint16_t base, uint8_t drive) {
    ata_wait_bsy(base);
    outb(base + 6, drive);
    outb(base + 2, 0);
    outb(base + 7, 0xEC);
    uint8_t s = ata_st(base);
    if (s == 0) return -1;
    if (ata_wait_drq(base)) return -1;
    uint16_t id[256];
    for (int i = 0; i < 256; i++) id[i] = inw(base);
    uint32_t secs = id[60] | ((uint32_t)id[61] << 16);
    if (!secs) return -1;
    return 0;
}

int ata_init(void) {
    uint16_t bases[] = {0x1F0, 0x170};
    uint8_t drives[] = {0xE0, 0xF0};

    for (int b = 0; b < 2; b++) {
        for (int d = 0; d < 2; d++) {
            uint16_t base = bases[b];
            uint8_t drive = drives[d];
            if (ata_probe(base, drive) == 0) {
                if (ata_count >= MAX_ATA_DRIVES) break;
                ata_privs[ata_count].base = base;
                ata_privs[ata_count].drive = drive;
                ata_wait_bsy(base);
                outb(base + 6, drive);
                outb(base + 2, 0);
                outb(base + 7, 0xEC);
                ata_wait_drq(base);
                uint16_t id[256];
                for (int i = 0; i < 256; i++) id[i] = inw(base);
                uint32_t secs = id[60] | ((uint32_t)id[61] << 16);
                ata_names[ata_count][0] = 'a';
                ata_names[ata_count][1] = 't';
                ata_names[ata_count][2] = 'a';
                ata_names[ata_count][3] = '0' + ata_count;
                ata_names[ata_count][4] = '\0';
                ata_devs[ata_count].name = ata_names[ata_count];
                ata_devs[ata_count].size = (uint64_t)secs * 512;
                ata_devs[ata_count].block_size = 512;
                ata_devs[ata_count].read = ata_read;
                ata_devs[ata_count].write = ata_write;
                ata_devs[ata_count].priv = &ata_privs[ata_count];
                blkdev_register(&ata_devs[ata_count]);
                con_puts("[ATA] Found drive ");
                con_puts(ata_names[ata_count]);
                con_puts("\n");
                blkdev_scan_partitions(&ata_devs[ata_count]);

                ata_count++;
            }
        }
    }

    if (ata_count == 0) {
        con_puts("[ATA] No drives found.\n");
        return -1;
    }
    return 0;
}
#endif
