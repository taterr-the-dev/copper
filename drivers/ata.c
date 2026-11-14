#include <autoconf.h>
#ifdef CONFIG_ATA
#include <kernel/blkdev.h>
#include <kernel/io.h>
#include <kernel/string.h>
#include <kernel/console.h>
#include <kernel/spinlock.h>

#define MAX_ATA_DRIVES 4

struct ata_chan {
    uint16_t base;
    spinlock_t lock __attribute__((aligned(64)));
};

static struct ata_chan ata_chans[2] = {
    { 0x1F0, SPINLOCK_INIT },
    { 0x170, SPINLOCK_INIT },
};

struct ata_priv {
    uint16_t base;
    uint8_t drive;
    struct ata_chan *chan;
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

static inline void ata_insw(uint16_t port, void *addr, uint32_t cnt) {
    __asm__ volatile("cld; rep insw" : "+D"(addr), "+c"(cnt) : "d"(port) : "memory");
}

static inline void ata_outsw(uint16_t port, const void *addr, uint32_t cnt) {
    __asm__ volatile("cld; rep outsw" : "+S"(addr), "+c"(cnt) : "d"(port) : "memory");
}

static void ata_sel(struct ata_priv *p, uint64_t lba, uint32_t n) {
    ata_wait_bsy(p->base);
    outb(p->base + 6, p->drive | ((lba >> 24) & 0x0F));
    outb(p->base + 2, n & 0xFF);
    outb(p->base + 3, lba & 0xFF);
    outb(p->base + 4, (lba >> 8) & 0xFF);
    outb(p->base + 5, (lba >> 16) & 0xFF);
}

static int ata_rd(struct ata_priv *p, uint64_t lba, uint32_t n, uint8_t *b) {
    ata_sel(p, lba, n);
    outb(p->base + 7, 0x20);
    ata_st(p->base); ata_st(p->base);
    for (uint32_t i = 0; i < n; i++) {
        if (ata_wait_drq(p->base)) return -1;
        ata_insw(p->base, b + (size_t)i * 512, 256);
    }
    return 0;
}

static int ata_wr(struct ata_priv *p, uint64_t lba, uint32_t n, const uint8_t *b) {
    ata_sel(p, lba, n);
    outb(p->base + 7, 0x30);
    ata_st(p->base); ata_st(p->base);
    for (uint32_t i = 0; i < n; i++) {
        if (ata_wait_drq(p->base)) return -1;
        ata_outsw(p->base, b + (size_t)i * 512, 256);
    }
    return ata_wait_bsy(p->base);
}

#define ATA_CHUNK 8

static int ata_read(struct blkdev *d, uint64_t off, void *buf, size_t len) {
    struct ata_priv *p = d->priv;
    uint64_t s = off / 512;
    uint32_t o = off % 512;
    size_t done = 0;
    uint8_t *out = buf;
    while (done < len) {
        uint64_t fl;
        size_t rem = len - done;
        if (o == 0 && rem >= 512) {
            uint8_t big[ATA_CHUNK * 512];
            uint32_t n = rem / 512;
            if (n > ATA_CHUNK) n = ATA_CHUNK;
            spin_lock_irqsave(&p->chan->lock, &fl);
            int r = ata_rd(p, s, n, big);
            spin_unlock_irqrestore(&p->chan->lock, fl);
            if (r) return -1;
            memcpy(out + done, big, (size_t)n * 512);
            done += (size_t)n * 512;
            s += n;
        } else {
            uint8_t t[512];
            spin_lock_irqsave(&p->chan->lock, &fl);
            int r = ata_rd(p, s, 1, t);
            spin_unlock_irqrestore(&p->chan->lock, fl);
            if (r) return -1;
            size_t c = 512 - o;
            if (c > rem) c = rem;
            memcpy(out + done, t + o, c);
            done += c;
            o = 0;
            s++;
        }
    }
    return 0;
}

static int ata_write(struct blkdev *d, uint64_t off, const void *buf, size_t len) {
    struct ata_priv *p = d->priv;
    uint64_t s = off / 512;
    uint32_t o = off % 512;
    size_t done = 0;
    const uint8_t *in = buf;
    while (done < len) {
        uint64_t fl;
        size_t rem = len - done;
        if (o == 0 && rem >= 512) {
            uint8_t big[ATA_CHUNK * 512];
            uint32_t n = rem / 512;
            if (n > ATA_CHUNK) n = ATA_CHUNK;
            memcpy(big, in + done, (size_t)n * 512);
            spin_lock_irqsave(&p->chan->lock, &fl);
            int r = ata_wr(p, s, n, big);
            spin_unlock_irqrestore(&p->chan->lock, fl);
            if (r) return -1;
            done += (size_t)n * 512;
            s += n;
        } else {
            uint8_t t[512];
            size_t c = 512 - o;
            if (c > rem) c = rem;
            spin_lock_irqsave(&p->chan->lock, &fl);
            if (ata_rd(p, s, 1, t)) {
                spin_unlock_irqrestore(&p->chan->lock, fl);
                return -1;
            }
            memcpy(t + o, in + done, c);
            int r = ata_wr(p, s, 1, t);
            spin_unlock_irqrestore(&p->chan->lock, fl);
            if (r) return -1;
            done += c;
            o = 0;
            s++;
        }
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
                ata_privs[ata_count].chan = &ata_chans[b];
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
