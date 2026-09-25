#include "ext.h"
#include "extents.h"

static uint8_t T[8192];

static uint32_t rd_ind(struct ext_sb *s, uint32_t blk, uint32_t idx)
{
    ext_read_blk(s, blk, T);
    uint32_t v;
    memcpy(&v, T + idx * 4, 4);
    return v;
}

uint32_t ext_get_iblock(struct ext_sb *s, uint8_t *in, uint32_t b, int alloc)
{
    uint32_t flags;
    memcpy(&flags, in + 32, 4);

    if (flags & EXT4_EXTENTS_FL) {
        if (alloc) {
            return ext4_extent_alloc(s, in, b);
        }
        return ext4_extent_lookup(s, in, b);
    }

    uint32_t pb = s->bs / 4;
    if (b < 12) {
        uint32_t v;
        memcpy(&v, in + 40 + b * 4, 4);
        if (!v && alloc) {
            v = ext_alloc_block(s);
            memcpy(in + 40 + b * 4, &v, 4);
        }
        return v;
    }
    b -= 12;
    if (b < pb) {
        uint32_t ind;
        memcpy(&ind, in + 88, 4);
        if (!ind) {
            if (!alloc)
                return 0;
            ind = ext_alloc_block(s);
            memcpy(in + 88, &ind, 4);
            memset(T, 0, s->bs);
            ext_write_blk(s, ind, T);
        }
        uint32_t v = rd_ind(s, ind, b);
        if (!v && alloc) {
            v = ext_alloc_block(s);
            ext_read_blk(s, ind, T);
            memcpy(T + b * 4, &v, 4);
            ext_write_blk(s, ind, T);
        }
        return v;
    }
    b -= pb;
    if (b < pb * pb) {
        uint32_t dind;
        memcpy(&dind, in + 92, 4);
        if (!dind) {
            if (!alloc)
                return 0;
            dind = ext_alloc_block(s);
            memcpy(in + 92, &dind, 4);
            memset(T, 0, s->bs);
            ext_write_blk(s, dind, T);
        }
        uint32_t i1 = b / pb, i2 = b % pb;
        uint32_t ind = rd_ind(s, dind, i1);
        if (!ind) {
            if (!alloc)
                return 0;
            ind = ext_alloc_block(s);
            ext_read_blk(s, dind, T);
            memcpy(T + i1 * 4, &ind, 4);
            ext_write_blk(s, dind, T);
            memset(T, 0, s->bs);
            ext_write_blk(s, ind, T);
        }
        uint32_t v = rd_ind(s, ind, i2);
        if (!v && alloc) {
            v = ext_alloc_block(s);
            ext_read_blk(s, ind, T);
            memcpy(T + i2 * 4, &v, 4);
            ext_write_blk(s, ind, T);
        }
        return v;
    }
    b -= pb * pb;
    if (b < pb * pb * pb) {
        uint32_t tind;
        memcpy(&tind, in + 96, 4);
        if (!tind) {
            if (!alloc)
                return 0;
            tind = ext_alloc_block(s);
            memcpy(in + 96, &tind, 4);
            memset(T, 0, s->bs);
            ext_write_blk(s, tind, T);
        }
        uint32_t i1 = b / (pb * pb), r = b % (pb * pb), i2 = r / pb, i3 = r % pb;
        uint32_t dind = rd_ind(s, tind, i1);
        if (!dind) {
            if (!alloc)
                return 0;
            dind = ext_alloc_block(s);
            ext_read_blk(s, tind, T);
            memcpy(T + i1 * 4, &dind, 4);
            ext_write_blk(s, tind, T);
            memset(T, 0, s->bs);
            ext_write_blk(s, dind, T);
        }
        uint32_t ind = rd_ind(s, dind, i2);
        if (!ind) {
            if (!alloc)
                return 0;
            ind = ext_alloc_block(s);
            ext_read_blk(s, dind, T);
            memcpy(T + i2 * 4, &ind, 4);
            ext_write_blk(s, dind, T);
            memset(T, 0, s->bs);
            ext_write_blk(s, ind, T);
        }
        uint32_t v = rd_ind(s, ind, i3);
        if (!v && alloc) {
            v = ext_alloc_block(s);
            ext_read_blk(s, ind, T);
            memcpy(T + i3 * 4, &v, 4);
            ext_write_blk(s, ind, T);
        }
        return v;
    }
    return 0;
}
