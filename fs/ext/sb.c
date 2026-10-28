#include "ext.h"
#include <kernel/blkdev.h>

extern int bcache_read(const void *dev, uint64_t disk_block, void *buf, uint32_t len);
extern void bcache_store(const void *dev, uint64_t disk_block, const void *buf, uint32_t len);
extern void bcache_invalidate(const void *dev, uint64_t disk_block);

static uint32_t rd32(const uint8_t *p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t rd16(const uint8_t *p)
{
    return p[0] | (p[1] << 8);
}

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = v & 0xFF;
    p[1] = (v >> 8) & 0xFF;
    p[2] = (v >> 16) & 0xFF;
    p[3] = (v >> 24) & 0xFF;
}

static void wr16(uint8_t *p, uint16_t v)
{
    p[0] = v & 0xFF;
    p[1] = (v >> 8) & 0xFF;
}

int rb(struct ext_sb *s, uint64_t o, void *b, size_t l)
{
    return s->dev->read(s->dev, o, b, l);
}

int wb(struct ext_sb *s, uint64_t o, const void *b, size_t l)
{
    if (!s->dev->write)
        return -1;
    return s->dev->write(s->dev, o, b, l);
}

int ext_read_at(struct ext_sb *s, uint64_t offset, void *out, size_t len)
{
    return rb(s, offset, out, len);
}

int ext_write_at(struct ext_sb *s, uint64_t offset, const void *in, size_t len)
{
    return wb(s, offset, in, len);
}

int ext_read_blk(struct ext_sb *s, uint32_t blk, void *buf)
{
    uint64_t disk_block = (uint64_t)blk * s->bs;
    if (bcache_read(s->dev, disk_block, buf, s->bs))
        return 0;
    int ret = rb(s, disk_block, buf, s->bs);
    if (ret == 0)
        bcache_store(s->dev, disk_block, buf, s->bs);
    return ret;
}

int ext_write_blk(struct ext_sb *s, uint32_t blk, const void *buf)
{
    uint64_t disk_block = (uint64_t)blk * s->bs;
    bcache_invalidate(s->dev, disk_block);
    return wb(s, disk_block, buf, s->bs);
}

static uint8_t *gd_ptr(struct ext_sb *s, uint32_t g)
{
    return s->gdt + (uint64_t)g * s->desc_size;
}

static void gd_flush(struct ext_sb *s, uint32_t g)
{
    uint8_t *gd = gd_ptr(s, g);
    ext_gd_csum_set(s, g, gd);
    wb(s, (uint64_t)s->bgdt_block * s->bs + (uint64_t)g * s->desc_size, gd, s->desc_size);
}

static int gd_wide(struct ext_sb *s)
{
    return (s->incompat & EXT_INCOMPAT_64BIT) && s->desc_size >= 64;
}

static uint32_t gd_get32(struct ext_sb *s, uint8_t *gd, int lo, int hi)
{
    uint32_t v = rd16(gd + lo);
    if (gd_wide(s))
        v |= (uint32_t)rd16(gd + hi) << 16;
    return v;
}

static void gd_set32(struct ext_sb *s, uint8_t *gd, int lo, int hi, uint32_t v)
{
    wr16(gd + lo, v & 0xFFFF);
    if (gd_wide(s))
        wr16(gd + hi, v >> 16);
}

static uint32_t gd_free_blocks(struct ext_sb *s, uint8_t *gd) { return gd_get32(s, gd, 12, 44); }
static uint32_t gd_free_inodes(struct ext_sb *s, uint8_t *gd) { return gd_get32(s, gd, 14, 46); }
static uint32_t gd_used_dirs(struct ext_sb *s, uint8_t *gd) { return gd_get32(s, gd, 16, 48); }
static uint32_t gd_itable_unused(struct ext_sb *s, uint8_t *gd) { return gd_get32(s, gd, 28, 50); }

static uint32_t gd_loc(struct ext_sb *s, uint8_t *gd, int lo, int hi)
{
    if (gd_wide(s) && rd32(gd + hi) != 0)
        return 0;
    return rd32(gd + lo);
}

int ext_read_inode(struct ext_sb *s, uint32_t ino, uint8_t *raw)
{
    if (ino == 0 || ino > s->inodes) {
        memset(raw, 0, s->inode_size);
        return -1;
    }
    uint32_t g = (ino - 1) / s->ipg, idx = (ino - 1) % s->ipg;
    if (g >= s->groups) {
        memset(raw, 0, s->inode_size);
        return -1;
    }
    uint32_t itbl = gd_loc(s, gd_ptr(s, g), 8, 40);
    if (!itbl) {
        memset(raw, 0, s->inode_size);
        return -1;
    }
    return rb(s, (uint64_t)itbl * s->bs + (uint64_t)idx * s->inode_size, raw, s->inode_size);
}

int ext_write_inode(struct ext_sb *s, uint32_t ino, const uint8_t *raw)
{
    if (ino == 0 || ino > s->inodes)
        return -1;
    uint32_t g = (ino - 1) / s->ipg, idx = (ino - 1) % s->ipg;
    if (g >= s->groups)
        return -1;
    uint32_t itbl = gd_loc(s, gd_ptr(s, g), 8, 40);
    if (!itbl)
        return -1;
    uint8_t tmp[EXT_INODE_MAX];
    memcpy(tmp, raw, s->inode_size);
    ext_inode_csum_set(s, ino, tmp);
    return wb(s, (uint64_t)itbl * s->bs + (uint64_t)idx * s->inode_size, tmp, s->inode_size);
}

static void sb_adjust(struct ext_sb *s, int dblocks, int dinodes)
{
    uint8_t sb[1024];
    if (rb(s, 1024, sb, 1024))
        return;
    uint64_t fb = rd32(sb + 0x0C);
    if (s->incompat & EXT_INCOMPAT_64BIT)
        fb |= (uint64_t)rd32(sb + 0x158) << 32;
    uint32_t fi = rd32(sb + 0x10);
    fb = (uint64_t)((int64_t)fb + dblocks);
    fi = (uint32_t)((int32_t)fi + dinodes);
    wr32(sb + 0x0C, (uint32_t)fb);
    if (s->incompat & EXT_INCOMPAT_64BIT)
        wr32(sb + 0x158, (uint32_t)(fb >> 32));
    wr32(sb + 0x10, fi);
    ext_sb_csum_set(s, sb);
    wb(s, 1024, sb, 1024);
}

static int has_super(struct ext_sb *s, uint32_t g)
{
    if (!(s->ro_compat & EXT_RO_SPARSE_SUPER))
        return 1;
    if (g <= 1)
        return 1;
    if (!(g & 1))
        return 0;
    for (uint32_t b = 3; b <= 7; b += 2) {
        uint64_t p = b;
        while (p < g)
            p *= b;
        if (p == g)
            return 1;
    }
    return 0;
}

static void mark_range(uint8_t *buf, uint32_t from, uint32_t to, uint32_t bpg)
{
    for (uint32_t i = from; i < to && i < bpg; i++)
        buf[i / 8] |= 1 << (i & 7);
}

static void init_block_bitmap(struct ext_sb *s, uint32_t g, uint8_t *buf)
{
    memset(buf, 0, s->bs);
    uint64_t gstart = (uint64_t)s->first_data_block + (uint64_t)g * s->bpg;
    uint64_t gend = gstart + s->bpg;
    if (gend > s->blocks)
        gend = s->blocks;
    if (has_super(s, g)) {
        uint32_t gdt_blocks = ((uint64_t)s->groups * s->desc_size + s->bs - 1) / s->bs;
        mark_range(buf, 0, 1 + gdt_blocks + s->reserved_gdt, s->bpg);
    }
    for (uint32_t h = 0; h < s->groups; h++) {
        uint8_t *gd = gd_ptr(s, h);
        uint64_t bbm = gd_loc(s, gd, 0, 32), ibm = gd_loc(s, gd, 4, 36), it = gd_loc(s, gd, 8, 40);
        if (bbm >= gstart && bbm < gend)
            mark_range(buf, bbm - gstart, bbm - gstart + 1, s->bpg);
        if (ibm >= gstart && ibm < gend)
            mark_range(buf, ibm - gstart, ibm - gstart + 1, s->bpg);
        for (uint32_t k = 0; k < s->itb_per_group; k++) {
            uint64_t b = it + k;
            if (it && b >= gstart && b < gend)
                mark_range(buf, b - gstart, b - gstart + 1, s->bpg);
        }
    }
    mark_range(buf, (uint32_t)(gend - gstart), s->bpg, s->bpg);
    for (uint32_t i = s->bpg; i < s->bs * 8; i++)
        buf[i / 8] |= 1 << (i & 7);
}

static void init_inode_bitmap(struct ext_sb *s, uint8_t *buf)
{
    memset(buf, 0, s->bs);
    for (uint32_t i = s->ipg; i < s->bs * 8; i++)
        buf[i / 8] |= 1 << (i & 7);
}

static uint32_t count_zero(const uint8_t *buf, uint32_t nbits)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < nbits; i++)
        if (!(buf[i / 8] & (1 << (i & 7))))
            n++;
    return n;
}

static uint32_t alloc_block_nolock(struct ext_sb *s, uint32_t goal)
{
    uint32_t sg = 0, sbit = 0;
    if (goal >= s->first_data_block && goal < s->blocks) {
        sg = (goal - s->first_data_block) / s->bpg;
        sbit = (goal - s->first_data_block) % s->bpg;
    }
    uint8_t *buf = kmalloc(s->bs);
    if (!buf)
        return 0;
    for (uint32_t pass = 0; pass <= s->groups; pass++) {
        uint32_t g = (sg + pass) % s->groups;
        uint8_t *gd = gd_ptr(s, g);
        uint32_t flags = rd16(gd + 18);
        uint32_t freeb = gd_free_blocks(s, gd);
        if (freeb == 0)
            continue;
        uint32_t bbm = gd_loc(s, gd, 0, 32);
        if (!bbm)
            continue;
        int uninit = (flags & EXT_BG_BLOCK_UNINIT) && (s->csum || s->gdt_csum);
        if (uninit)
            init_block_bitmap(s, g, buf);
        else if (rb(s, (uint64_t)bbm * s->bs, buf, s->bs))
            continue;
        uint64_t gstart = (uint64_t)s->first_data_block + (uint64_t)g * s->bpg;
        uint32_t limit = s->bpg;
        if (gstart + limit > s->blocks)
            limit = s->blocks - gstart;
        uint32_t from = (pass == 0) ? sbit : 0;
        for (uint32_t i = from; i < limit; i++) {
            if (buf[i / 8] & (1 << (i & 7)))
                continue;
            buf[i / 8] |= 1 << (i & 7);
            if (uninit)
                freeb = count_zero(buf, limit) + 1;
            wb(s, (uint64_t)bbm * s->bs, buf, s->bs);
            bcache_invalidate(s->dev, (uint64_t)bbm * s->bs);
            gd_set32(s, gd, 12, 44, freeb - 1);
            wr16(gd + 18, flags & ~EXT_BG_BLOCK_UNINIT);
            ext_bitmap_csum_set(s, gd, buf, 0);
            gd_flush(s, g);
            sb_adjust(s, -1, 0);
            kfree(buf);
            return (uint32_t)(gstart + i);
        }
    }
    kfree(buf);
    return 0;
}

static uint32_t alloc_inode_nolock(struct ext_sb *s, int is_dir)
{
    uint8_t *buf = kmalloc(s->bs);
    if (!buf)
        return 0;
    for (uint32_t g = 0; g < s->groups; g++) {
        uint8_t *gd = gd_ptr(s, g);
        uint32_t flags = rd16(gd + 18);
        uint32_t freei = gd_free_inodes(s, gd);
        if (freei == 0)
            continue;
        uint32_t ibm = gd_loc(s, gd, 4, 36);
        if (!ibm)
            continue;
        int uninit = (flags & EXT_BG_INODE_UNINIT) && (s->csum || s->gdt_csum);
        if (uninit)
            init_inode_bitmap(s, buf);
        else if (rb(s, (uint64_t)ibm * s->bs, buf, s->bs))
            continue;
        for (uint32_t i = 0; i < s->ipg; i++) {
            if (buf[i / 8] & (1 << (i & 7)))
                continue;
            buf[i / 8] |= 1 << (i & 7);
            if (uninit)
                freei = count_zero(buf, s->ipg) + 1;
            wb(s, (uint64_t)ibm * s->bs, buf, s->bs);
            gd_set32(s, gd, 14, 46, freei - 1);
            if (is_dir)
                gd_set32(s, gd, 16, 48, gd_used_dirs(s, gd) + 1);
            if (s->csum || s->gdt_csum) {
                uint32_t unused = gd_itable_unused(s, gd);
                if (uninit)
                    unused = s->ipg;
                if (s->ipg - unused < i + 1)
                    unused = s->ipg - (i + 1);
                gd_set32(s, gd, 28, 50, unused);
            }
            wr16(gd + 18, flags & ~EXT_BG_INODE_UNINIT);
            ext_bitmap_csum_set(s, gd, buf, 1);
            gd_flush(s, g);
            sb_adjust(s, 0, -1);
            kfree(buf);
            return g * s->ipg + i + 1;
        }
    }
    kfree(buf);
    return 0;
}

static void free_block_nolock(struct ext_sb *s, uint32_t blk)
{
    if (blk < s->first_data_block || blk >= s->blocks)
        return;
    uint32_t g = (blk - s->first_data_block) / s->bpg;
    uint32_t bit = (blk - s->first_data_block) % s->bpg;
    uint8_t *gd = gd_ptr(s, g);
    uint32_t bbm = gd_loc(s, gd, 0, 32);
    if (!bbm)
        return;
    uint8_t *buf = kmalloc(s->bs);
    if (!buf)
        return;
    uint32_t flags = rd16(gd + 18);
    if ((flags & EXT_BG_BLOCK_UNINIT) && (s->csum || s->gdt_csum)) {
        kfree(buf);
        return;
    }
    if (rb(s, (uint64_t)bbm * s->bs, buf, s->bs)) {
        kfree(buf);
        return;
    }
    if (buf[bit / 8] & (1 << (bit & 7))) {
        buf[bit / 8] &= ~(1 << (bit & 7));
        wb(s, (uint64_t)bbm * s->bs, buf, s->bs);
        gd_set32(s, gd, 12, 44, gd_free_blocks(s, gd) + 1);
        ext_bitmap_csum_set(s, gd, buf, 0);
        gd_flush(s, g);
        sb_adjust(s, 1, 0);
    }
    bcache_invalidate(s->dev, (uint64_t)blk * s->bs);
    kfree(buf);
}

static void free_range_nolock(struct ext_sb *s, uint32_t start, uint32_t count)
{
    uint8_t *buf = kmalloc(s->bs);
    if (!buf)
        return;
    while (count) {
        if (start < s->first_data_block || start >= s->blocks)
            break;
        uint32_t g = (start - s->first_data_block) / s->bpg;
        uint32_t bit = (start - s->first_data_block) % s->bpg;
        uint32_t n = s->bpg - bit;
        if (n > count)
            n = count;
        uint8_t *gd = gd_ptr(s, g);
        uint32_t bbm = gd_loc(s, gd, 0, 32);
        uint32_t flags = rd16(gd + 18);
        int uninit = (flags & EXT_BG_BLOCK_UNINIT) && (s->csum || s->gdt_csum);
        if (bbm && !uninit && rb(s, (uint64_t)bbm * s->bs, buf, s->bs) == 0) {
            uint32_t freed = 0;
            for (uint32_t i = 0; i < n; i++) {
                uint32_t b = bit + i;
                if (buf[b / 8] & (1 << (b & 7))) {
                    buf[b / 8] &= ~(1 << (b & 7));
                    freed++;
                }
                bcache_invalidate(s->dev, (uint64_t)(start + i) * s->bs);
            }
            if (freed) {
                wb(s, (uint64_t)bbm * s->bs, buf, s->bs);
                gd_set32(s, gd, 12, 44, gd_free_blocks(s, gd) + freed);
                ext_bitmap_csum_set(s, gd, buf, 0);
                gd_flush(s, g);
                sb_adjust(s, freed, 0);
            }
        }
        start += n;
        count -= n;
    }
    kfree(buf);
}

static void free_inode_nolock(struct ext_sb *s, uint32_t ino, int was_dir)
{
    if (ino == 0 || ino > s->inodes)
        return;
    uint32_t g = (ino - 1) / s->ipg, idx = (ino - 1) % s->ipg;
    uint8_t *gd = gd_ptr(s, g);
    uint32_t ibm = gd_loc(s, gd, 4, 36);
    if (!ibm)
        return;
    uint8_t *buf = kmalloc(s->bs);
    if (!buf)
        return;
    if (rb(s, (uint64_t)ibm * s->bs, buf, s->bs)) {
        kfree(buf);
        return;
    }
    if (buf[idx / 8] & (1 << (idx & 7))) {
        buf[idx / 8] &= ~(1 << (idx & 7));
        wb(s, (uint64_t)ibm * s->bs, buf, s->bs);
        gd_set32(s, gd, 14, 46, gd_free_inodes(s, gd) + 1);
        if (was_dir && gd_used_dirs(s, gd) > 0)
            gd_set32(s, gd, 16, 48, gd_used_dirs(s, gd) - 1);
        ext_bitmap_csum_set(s, gd, buf, 1);
        gd_flush(s, g);
        sb_adjust(s, 0, 1);
    }
    kfree(buf);
}

uint32_t ext_alloc_block_goal(struct ext_sb *s, uint32_t goal)
{
    uint64_t flags;
    spin_lock_irqsave(&s->lock, &flags);
    uint32_t r = alloc_block_nolock(s, goal);
    spin_unlock_irqrestore(&s->lock, flags);
    return r;
}

uint32_t ext_alloc_block(struct ext_sb *s)
{
    return ext_alloc_block_goal(s, 0);
}

void ext_free_block(struct ext_sb *s, uint32_t blk)
{
    uint64_t flags;
    spin_lock_irqsave(&s->lock, &flags);
    free_block_nolock(s, blk);
    spin_unlock_irqrestore(&s->lock, flags);
}

void ext_free_range(struct ext_sb *s, uint32_t start, uint32_t count)
{
    uint64_t flags;
    spin_lock_irqsave(&s->lock, &flags);
    free_range_nolock(s, start, count);
    spin_unlock_irqrestore(&s->lock, flags);
}

uint32_t ext_alloc_inode(struct ext_sb *s, int is_dir)
{
    uint64_t flags;
    spin_lock_irqsave(&s->lock, &flags);
    uint32_t r = alloc_inode_nolock(s, is_dir);
    spin_unlock_irqrestore(&s->lock, flags);
    return r;
}

void ext_free_inode(struct ext_sb *s, uint32_t ino, int was_dir)
{
    uint64_t flags;
    spin_lock_irqsave(&s->lock, &flags);
    free_inode_nolock(s, ino, was_dir);
    spin_unlock_irqrestore(&s->lock, flags);
}

int ext_init_sb(struct blkdev *dev, void **sbp)
{
    uint8_t sb[1024];
    if (dev->read(dev, 1024, sb, 1024))
        return -1;
    if (rd16(sb + 56) != 0xEF53)
        return -1;

    struct ext_sb *st = kmalloc(sizeof(struct ext_sb));
    if (!st)
        return -1;

    st->dev = dev;
    spinlock_init(&st->lock);

    uint32_t log = rd32(sb + 0x18);
    if (log > 2) {
        kfree(st);
        return -1;
    }
    st->bs = 1024 << log;
    st->inodes = rd32(sb + 0x00);
    st->blocks = rd32(sb + 0x04);
    st->first_data_block = rd32(sb + 0x14);
    st->bpg = rd32(sb + 0x20);
    st->ipg = rd32(sb + 0x28);
    st->compat = rd32(sb + 0x5C);
    st->incompat = rd32(sb + 0x60);
    st->ro_compat = rd32(sb + 0x64);

    uint32_t rev = rd32(sb + 0x4C);
    st->inode_size = (rev >= 1) ? rd16(sb + 0x58) : 128;
    if (st->inode_size == 0)
        st->inode_size = 128;

    if (st->bpg == 0 || st->ipg == 0 || st->inode_size > EXT_INODE_MAX || st->inode_size < 128 ||
        st->bpg > st->bs * 8 || st->ipg > st->bs * 8) {
        kfree(st);
        return -1;
    }
    if ((st->incompat & (EXT_INCOMPAT_META_BG | EXT_INCOMPAT_INLINE_DATA | EXT_INCOMPAT_JOURNAL_DEV)) ||
        (st->ro_compat & EXT_RO_BIGALLOC)) {
        con_puts("[EXT] unsupported filesystem features, refusing to mount\n");
        kfree(st);
        return -1;
    }
    if ((st->incompat & EXT_INCOMPAT_64BIT) && rd32(sb + 0x150) != 0) {
        con_puts("[EXT] filesystem larger than 2^32 blocks not supported\n");
        kfree(st);
        return -1;
    }

    st->groups = (st->blocks - st->first_data_block + st->bpg - 1) / st->bpg;
    st->bgdt_block = st->first_data_block + 1;
    if (st->incompat & EXT_INCOMPAT_64BIT) {
        uint16_t ds = rd16(sb + 0xFE);
        st->desc_size = ds < 64 ? 64 : ds;
    } else {
        st->desc_size = 32;
    }
    if (st->desc_size > 64)
        st->desc_size = 64;
    st->reserved_gdt = rd16(sb + 0xCE);
    st->itb_per_group = ((uint64_t)st->ipg * st->inode_size + st->bs - 1) / st->bs;
    st->extra_isize = 0;
    if (st->inode_size > 128) {
        uint16_t want = rd16(sb + 0x15E);
        st->extra_isize = want ? want : 32;
        if (st->extra_isize > st->inode_size - 128)
            st->extra_isize = st->inode_size - 128;
    }
    ext_csum_init(st, sb);

    uint32_t gdt_bytes = st->groups * st->desc_size;
    st->gdt = kmalloc(gdt_bytes);
    if (!st->gdt) {
        kfree(st);
        return -1;
    }
    if (dev->read(dev, (uint64_t)st->bgdt_block * st->bs, st->gdt, gdt_bytes)) {
        kfree(st->gdt);
        kfree(st);
        return -1;
    }

    *sbp = st;
    return 0;
}
