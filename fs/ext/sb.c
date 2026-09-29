#include "ext.h"
#include <kernel/blkdev.h>
extern void put_u64(uint64_t v);

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

extern int bcache_read(uint64_t disk_block, void *buf);
extern void bcache_store(uint64_t disk_block, const void *buf);
extern void bcache_invalidate(uint64_t disk_block);

int ext_read_blk(struct ext_sb *s, uint32_t blk, void *buf) {
    uint64_t disk_block = (uint64_t)blk * s->bs;
    
    if (bcache_read(disk_block, buf)) {
        return 0;
    }
    
    int ret = rb(s, disk_block, buf, s->bs);
    
    if (ret == 0) {
        bcache_store(disk_block, buf);
    }
    
    return ret;
}

int ext_write_blk(struct ext_sb *s, uint32_t blk, const void *buf) {
    uint64_t disk_block = (uint64_t)blk * s->bs;
    
    bcache_invalidate(disk_block);
    
    return wb(s, disk_block, buf, s->bs);
}

int ext_read_inode(struct ext_sb *s, uint32_t ino, uint8_t *raw)
{
    uint32_t g = (ino - 1) / s->ipg, idx = (ino - 1) % s->ipg;
    uint8_t bgd[64];
    uint32_t read_sz = s->desc_size > 64 ? 64 : s->desc_size;

    rb(s, (uint64_t)s->bgdt_block * s->bs + (uint64_t)g * s->desc_size, bgd, read_sz);

    uint32_t itbl = bgd[8] | (bgd[9] << 8) | (bgd[10] << 16) | ((uint32_t)bgd[11] << 24);

    uint64_t offset = (uint64_t)itbl * s->bs + (uint64_t)idx * s->inode_size;
    int res = rb(s, offset, raw, s->inode_size);
    uint16_t mode = raw[0] | (raw[1] << 8);
    uint32_t size = raw[4] | (raw[5] << 8) | (raw[6] << 16) | ((uint32_t)raw[7] << 24);
    return res;
}

int ext_write_inode(struct ext_sb *s, uint32_t ino, const uint8_t *raw)
{
    uint32_t g = (ino - 1) / s->ipg, idx = (ino - 1) % s->ipg;
    uint8_t bgd[64];
    uint32_t read_sz = s->desc_size > 64 ? 64 : s->desc_size;
    rb(s, (uint64_t)s->bgdt_block * s->bs + (uint64_t)g * s->desc_size, bgd, read_sz);
    uint32_t itbl = bgd[8] | (bgd[9] << 8) | (bgd[10] << 16) | ((uint32_t)bgd[11] << 24);
    return wb(s, (uint64_t)itbl * s->bs + (uint64_t)idx * s->inode_size, raw, s->inode_size);
}

static int bit_get(struct ext_sb *s, uint32_t bmp_blk, uint32_t bit)
{
    uint8_t b;
    rb(s, (uint64_t)bmp_blk * s->bs + bit / 8, &b, 1);
    return (b >> (bit & 7)) & 1;
}

static void bit_set(struct ext_sb *s, uint32_t bmp_blk, uint32_t bit)
{
    uint8_t b;
    rb(s, (uint64_t)bmp_blk * s->bs + bit / 8, &b, 1);
    b |= 1 << (bit & 7);
    wb(s, (uint64_t)bmp_blk * s->bs + bit / 8, &b, 1);
}

uint32_t ext_alloc_block(struct ext_sb *s)
{
    uint32_t num_groups = (s->blocks + s->bpg - 1) / s->bpg;
    uint8_t bgd[64];
    uint32_t read_sz = s->desc_size > 64 ? 64 : s->desc_size;

    for (uint32_t g = 0; g < num_groups; g++) {
        rb(s, (uint64_t)s->bgdt_block * s->bs + (uint64_t)g * s->desc_size, bgd, read_sz);
        uint16_t free_count = bgd[12] | (bgd[13] << 8);
        if (free_count == 0)
            continue;

        uint32_t bb = bgd[0] | (bgd[1] << 8) | (bgd[2] << 16) | ((uint32_t)bgd[3] << 24);

        uint32_t bmp_bytes = (s->bpg + 7) / 8;
        if (bmp_bytes > 8192)
            bmp_bytes = 8192;

        uint8_t *buf = kmalloc(bmp_bytes);
        if (!buf)
            return 0;
        rb(s, (uint64_t)bb * s->bs, buf, bmp_bytes);
        for (uint32_t i = 0; i < s->bpg; i++) {
            if (!(buf[i / 8] & (1 << (i & 7)))) {
                buf[i / 8] |= (1 << (i & 7));
                wb(s, (uint64_t)bb * s->bs, buf, bmp_bytes);
                bcache_invalidate((uint64_t)bb * s->bs);
                kfree(buf);
                free_count--;
                bgd[12] = free_count & 0xFF;
                bgd[13] = (free_count >> 8) & 0xFF;
                uint64_t bgd_offset = (uint64_t)s->bgdt_block * s->bs + (uint64_t)g * s->desc_size;
                uint32_t bgd_blk = bgd_offset / s->bs;
                wb(s, bgd_offset, bgd, read_sz);
                bcache_invalidate((uint64_t)bgd_blk * s->bs);
                uint32_t abs_blk = (g * s->bpg) + i;
                if (abs_blk == 0) continue;
                return abs_blk;
            }
        }
        kfree(buf);
    }
    return 0;
}

uint32_t ext_alloc_inode(struct ext_sb *s)
{
    uint32_t num_groups = (s->inodes + s->ipg - 1) / s->ipg;
    uint8_t bgd[64];
    uint32_t read_sz = s->desc_size > 64 ? 64 : s->desc_size;

    for (uint32_t g = 0; g < num_groups; g++) {
        rb(s, (uint64_t)s->bgdt_block * s->bs + (uint64_t)g * s->desc_size, bgd, read_sz);
        uint16_t free_count = bgd[14] | (bgd[15] << 8);
        if (free_count == 0)
            continue;

        uint32_t ib = bgd[4] | (bgd[5] << 8) | (bgd[6] << 16) | ((uint32_t)bgd[7] << 24);
        uint32_t bmp_bytes = (s->ipg + 7) / 8;
        if (bmp_bytes > 8192)
            bmp_bytes = 8192;
        uint8_t *buf = kmalloc(bmp_bytes);
        if (!buf)
            return 0;
        rb(s, (uint64_t)ib * s->bs, buf, bmp_bytes);
        for (uint32_t i = 0; i < s->ipg; i++) {
            if (!(buf[i / 8] & (1 << (i & 7)))) {
                buf[i / 8] |= (1 << (i & 7));
                wb(s, (uint64_t)ib * s->bs, buf, bmp_bytes);
                bcache_invalidate((uint64_t)ib * s->bs);
                kfree(buf);
                free_count--;
                bgd[14] = free_count & 0xFF;
                bgd[15] = (free_count >> 8) & 0xFF;
                uint64_t bgd_offset = (uint64_t)s->bgdt_block * s->bs + (uint64_t)g * s->desc_size;
                uint32_t bgd_blk = bgd_offset / s->bs;
                wb(s, bgd_offset, bgd, read_sz);
                bcache_invalidate((uint64_t)bgd_blk * s->bs);
                return (g * s->ipg) + i + 1;
            }
        }
        kfree(buf);
    }
    return 0;
}

int ext_init_sb(struct blkdev *dev, void **sbp)
{
    uint8_t sb[1024];
    if (dev->read(dev, 1024, sb, 1024)) {
        return -1;
    }
    if ((sb[56] | (sb[57] << 8)) != 0xEF53) {
        return -1;
    }

    struct ext_sb *st = kmalloc(sizeof(struct ext_sb));
    if (!st)
        return -1;

    st->dev = dev;

    uint32_t log = sb[0x18] | (sb[0x19] << 8) | (sb[0x1A] << 16) | ((uint32_t)sb[0x1B] << 24);
    st->bs = 1024 << log;

    st->inodes = sb[0x00] | (sb[0x01] << 8) | (sb[0x02] << 16) | ((uint32_t)sb[0x03] << 24);
    st->blocks = sb[0x04] | (sb[0x05] << 8) | (sb[0x06] << 16) | ((uint32_t)sb[0x07] << 24);
    st->ipg = sb[0x28] | (sb[0x29] << 8) | (sb[0x2A] << 16) | ((uint32_t)sb[0x2B] << 24);
    st->bpg = sb[0x20] | (sb[0x21] << 8) | (sb[0x22] << 16) | ((uint32_t)sb[0x23] << 24);

    uint32_t rev = sb[0x4C] | (sb[0x4D] << 8) | (sb[0x4E] << 16) | ((uint32_t)sb[0x4F] << 24);
    st->inode_size = (rev >= 1) ? (sb[0x58] | (sb[0x59] << 8)) : 128;
    if (st->inode_size == 0)
        st->inode_size = 128;

    st->bgdt_block = (st->bs == 1024) ? 2 : 1;

    uint32_t fic = sb[0x60] | (sb[0x61] << 8) | (sb[0x62] << 16) | ((uint32_t)sb[0x63] << 24);
    uint16_t desc_size = sb[0xFE] | (sb[0xFF] << 8);
    if (fic & 0x0040) {
        st->desc_size = 64;
    } else {
        st->desc_size = desc_size ? desc_size : 32;
    }

    *sbp = st;
    return 0;
}
