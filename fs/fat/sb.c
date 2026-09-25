#include "fat.h"
#include <kernel/blkdev.h>

uint8_t fat_sec_buf[8192];

int fat_read_sector(struct fat_sb *s, uint64_t sec_, void *b, int n)
{
    return s->dev->read(s->dev, sec_ * s->bps, b, n * s->bps);
}

int fat_write_sector(struct fat_sb *s, uint64_t sec_, const void *b, int n)
{
    if (!s->dev->write)
        return -1;
    return s->dev->write(s->dev, sec_ * s->bps, b, n * s->bps);
}

uint64_t fat_cl_sec(struct fat_sb *s, uint32_t c)
{
    return s->datasec + (c - 2) * s->spc;
}

int fat_read_cl(struct fat_sb *s, uint32_t c, void *b)
{
    return fat_read_sector(s, fat_cl_sec(s, c), b, s->spc);
}

int fat_write_cl(struct fat_sb *s, uint32_t c, const void *b)
{
    return fat_write_sector(s, fat_cl_sec(s, c), b, s->spc);
}

uint32_t fat_next_cl(struct fat_sb *s, uint32_t c)
{
    if (s->fat_type == 32) {
        uint64_t off = s->reserved * s->bps + (uint64_t)c * 4;
        uint8_t b[4];
        s->dev->read(s->dev, off, b, 4);
        return (b[0] | (b[1] << 8) | (b[2] << 16) | ((uint32_t)b[3] << 24)) & 0x0FFFFFFF;
    } else if (s->fat_type == 16) {
        uint64_t off = s->reserved * s->bps + (uint64_t)c * 2;
        uint8_t b[2];
        s->dev->read(s->dev, off, b, 2);
        return b[0] | (b[1] << 8);
    } else {
        uint64_t off = s->reserved * s->bps + c + (c / 2);
        uint8_t b[2];
        s->dev->read(s->dev, off, b, 2);
        uint32_t next = b[0] | (b[1] << 8);
        return (c % 2 == 0) ? (next & 0x0FFF) : (next >> 4);
    }
}

void fat_set_fat(struct fat_sb *s, uint32_t c, uint32_t v)
{
    if (s->fat_type == 32) {
        uint64_t off = s->reserved * s->bps + (uint64_t)c * 4;
        uint8_t b[4] = {v & 0xff, (v >> 8) & 0xff, (v >> 16) & 0xff, (v >> 24) & 0xff};
        s->dev->write(s->dev, off, b, 4);
        if (s->nfats > 1)
            s->dev->write(s->dev, off + s->fatsz * s->bps, b, 4);
    } else if (s->fat_type == 16) {
        uint64_t off = s->reserved * s->bps + (uint64_t)c * 2;
        uint8_t b[2] = {v & 0xff, (v >> 8) & 0xff};
        s->dev->write(s->dev, off, b, 2);
        if (s->nfats > 1)
            s->dev->write(s->dev, off + s->fatsz * s->bps, b, 2);
    } else {
        uint64_t off = s->reserved * s->bps + c + (c / 2);
        uint8_t b[2];
        s->dev->read(s->dev, off, b, 2);
        if (c % 2 == 0) {
            b[0] = v & 0xff;
            b[1] = (b[1] & 0xF0) | ((v >> 8) & 0x0F);
        } else {
            b[0] = (b[0] & 0x0F) | ((v & 0x0F) << 4);
            b[1] = (v >> 4) & 0xff;
        }
        s->dev->write(s->dev, off, b, 2);
        if (s->nfats > 1)
            s->dev->write(s->dev, off + s->fatsz * s->bps, b, 2);
    }
}

uint32_t fat_alloc_cl(struct fat_sb *s)
{
    for (uint32_t c = 2; c < s->total_cl + 2; c++) {
        if (fat_next_cl(s, c) == 0) {
            fat_set_fat(s, c,
                        s->fat_type == 12 ? 0x0FFF : (s->fat_type == 16 ? 0xFFFF : 0x0FFFFFFF));
            return c;
        }
    }
    return 0;
}

int fat_init_sb(struct blkdev *dev, void **sbp, int force_type)
{
    uint8_t b[512];
    if (dev->read(dev, 0, b, 512))
        return -1;
    if (b[510] != 0x55 || b[511] != 0xAA)
        return -1;

    struct fat_sb *st = kmalloc(sizeof(struct fat_sb));
    if (!st)
        return -1;

    st->dev = dev;
    st->bps = b[11] | (b[12] << 8);
    st->spc = b[13];
    st->reserved = b[14] | (b[15] << 8);
    st->nfats = b[16];
    st->root_ents = b[17] | (b[18] << 8);
    uint32_t tot_sec16 = b[19] | (b[20] << 8);
    st->fatsz = b[22] | (b[23] << 8);
    if (st->fatsz == 0)
        st->fatsz = b[36] | (b[37] << 8) | (b[38] << 16) | ((uint32_t)b[39] << 24);

    uint32_t total_sectors =
        tot_sec16 ? tot_sec16 : (b[32] | (b[33] << 8) | (b[34] << 16) | ((uint32_t)b[35] << 24));

    st->datasec = st->reserved + st->nfats * st->fatsz;
    uint32_t data_sectors = total_sectors - st->datasec;
    st->total_cl = data_sectors / st->spc;

    if (force_type > 0) {
        st->fat_type = force_type;
    } else {
        if (st->total_cl < 4085)
            st->fat_type = 12;
        else if (st->total_cl < 65525)
            st->fat_type = 16;
        else
            st->fat_type = 32;
    }

    if (st->fat_type == 32) {
        st->rootcl = b[44] | (b[45] << 8) | (b[46] << 16) | ((uint32_t)b[47] << 24);
        st->root_sec = 0;
    } else {
        st->rootcl = 0;
        st->root_sec = st->reserved;
    }

    *sbp = st;
    return 0;
}
