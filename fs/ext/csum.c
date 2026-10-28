#include "ext.h"

static uint32_t crc_tab[256];
static int crc_ready;

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

static void crc_init(void)
{
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++)
            c = (c & 1) ? (c >> 1) ^ 0x82F63B78U : c >> 1;
        crc_tab[i] = c;
    }
    crc_ready = 1;
}

uint32_t ext_crc32c(uint32_t crc, const void *buf, size_t len)
{
    if (!crc_ready)
        crc_init();
    const uint8_t *p = buf;
    for (size_t i = 0; i < len; i++)
        crc = (crc >> 8) ^ crc_tab[(crc ^ p[i]) & 0xFF];
    return crc;
}

static uint16_t crc16_update(uint16_t crc, const void *buf, size_t len)
{
    const uint8_t *p = buf;
    for (size_t i = 0; i < len; i++) {
        crc ^= p[i];
        for (int k = 0; k < 8; k++)
            crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
    }
    return crc;
}

void ext_csum_init(struct ext_sb *s, const uint8_t *sb)
{
    memcpy(s->uuid, sb + 0x68, 16);
    s->csum = (s->ro_compat & EXT_RO_METADATA_CSUM) ? 1 : 0;
    s->gdt_csum = (!s->csum && (s->ro_compat & EXT_RO_GDT_CSUM)) ? 1 : 0;
    if (s->incompat & EXT_INCOMPAT_CSUM_SEED)
        s->csum_seed = rd32(sb + 0x270);
    else
        s->csum_seed = ext_crc32c(~0U, s->uuid, 16);
}

void ext_sb_csum_set(struct ext_sb *s, uint8_t *sb)
{
    if (!s->csum)
        return;
    wr32(sb + 0x3FC, ext_crc32c(~0U, sb, 0x3FC));
}

void ext_gd_csum_set(struct ext_sb *s, uint32_t g, uint8_t *gd)
{
    uint8_t gle[4];
    wr32(gle, g);
    if (s->csum) {
        uint8_t zero[2] = {0, 0};
        uint32_t c = ext_crc32c(s->csum_seed, gle, 4);
        c = ext_crc32c(c, gd, 0x1E);
        c = ext_crc32c(c, zero, 2);
        if (s->desc_size > 0x20)
            c = ext_crc32c(c, gd + 0x20, s->desc_size - 0x20);
        wr16(gd + 0x1E, c & 0xFFFF);
    } else if (s->gdt_csum) {
        uint16_t c = crc16_update(0xFFFF, s->uuid, 16);
        c = crc16_update(c, gle, 4);
        c = crc16_update(c, gd, 0x1E);
        if ((s->incompat & EXT_INCOMPAT_64BIT) && s->desc_size > 0x20)
            c = crc16_update(c, gd + 0x20, s->desc_size - 0x20);
        wr16(gd + 0x1E, c);
    }
}

void ext_bitmap_csum_set(struct ext_sb *s, uint8_t *gd, const uint8_t *bmp, int inode_bitmap)
{
    if (!s->csum)
        return;
    uint32_t len = (inode_bitmap ? s->ipg : s->bpg) / 8;
    uint32_t c = ext_crc32c(s->csum_seed, bmp, len);
    if (inode_bitmap) {
        wr16(gd + 0x1A, c & 0xFFFF);
        if (s->desc_size >= 0x3C)
            wr16(gd + 0x3A, c >> 16);
    } else {
        wr16(gd + 0x18, c & 0xFFFF);
        if (s->desc_size >= 0x3A)
            wr16(gd + 0x38, c >> 16);
    }
}

static int inode_has_csum_hi(struct ext_sb *s, const uint8_t *in)
{
    return s->inode_size > 128 && rd16(in + 0x80) >= 4;
}

void ext_inode_csum_set(struct ext_sb *s, uint32_t ino, uint8_t *in)
{
    if (!s->csum)
        return;
    uint8_t le[4], zero[4] = {0, 0, 0, 0};
    int hi = inode_has_csum_hi(s, in);
    wr32(le, ino);
    uint32_t c = ext_crc32c(s->csum_seed, le, 4);
    c = ext_crc32c(c, in + 0x64, 4);
    c = ext_crc32c(c, in, 0x7C);
    c = ext_crc32c(c, zero, 2);
    if (hi) {
        c = ext_crc32c(c, in + 0x7E, 0x82 - 0x7E);
        c = ext_crc32c(c, zero, 2);
        c = ext_crc32c(c, in + 0x84, s->inode_size - 0x84);
    } else {
        c = ext_crc32c(c, in + 0x7E, s->inode_size - 0x7E);
    }
    wr16(in + 0x7C, c & 0xFFFF);
    if (hi)
        wr16(in + 0x82, c >> 16);
}

uint32_t ext_inode_seed(struct ext_sb *s, uint32_t ino, const uint8_t *in)
{
    uint8_t le[4];
    wr32(le, ino);
    uint32_t c = ext_crc32c(s->csum_seed, le, 4);
    return ext_crc32c(c, in + 0x64, 4);
}

void ext_extent_blk_csum_set(struct ext_sb *s, uint32_t ino, const uint8_t *in, uint8_t *buf)
{
    if (!s->csum)
        return;
    uint32_t off = 12 + (uint32_t)rd16(buf + 4) * 12;
    if (off + 4 > s->bs)
        return;
    wr32(buf + off, ext_crc32c(ext_inode_seed(s, ino, in), buf, off));
}

int ext_dir_tail_present(struct ext_sb *s, const uint8_t *buf)
{
    const uint8_t *t = buf + s->bs - 12;
    return rd32(t) == 0 && rd16(t + 4) == 12 && t[6] == 0 && t[7] == 0xDE;
}

void ext_dir_blk_csum_set(struct ext_sb *s, uint32_t ino, const uint8_t *in, uint8_t *buf)
{
    if (!s->csum || !ext_dir_tail_present(s, buf))
        return;
    wr32(buf + s->bs - 4, ext_crc32c(ext_inode_seed(s, ino, in), buf, s->bs - 12));
}

int ext_write_ext_blk(struct ext_sb *s, uint32_t ino, const uint8_t *in, uint32_t blk, uint8_t *buf)
{
    ext_extent_blk_csum_set(s, ino, in, buf);
    return ext_write_blk(s, blk, buf);
}

int ext_write_dir_blk(struct ext_sb *s, uint32_t ino, const uint8_t *in, uint32_t blk, uint8_t *buf)
{
    ext_dir_blk_csum_set(s, ino, in, buf);
    return ext_write_blk(s, blk, buf);
}
