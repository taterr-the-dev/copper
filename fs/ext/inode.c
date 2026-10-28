#include "ext.h"
#include "extents.h"

static uint32_t rd32(const uint8_t *p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = v & 0xFF;
    p[1] = (v >> 8) & 0xFF;
    p[2] = (v >> 16) & 0xFF;
    p[3] = (v >> 24) & 0xFF;
}

void ext_inode_add_blocks(struct ext_sb *s, uint8_t *in, int delta)
{
    int64_t cur = rd32(in + 28);
    cur += (int64_t)delta * (s->bs / 512);
    if (cur < 0)
        cur = 0;
    wr32(in + 28, (uint32_t)cur);
}

int ext_inode_has_blocks(struct ext_sb *s, const uint8_t *in)
{
    (void)s;
    return rd32(in + 28) != 0;
}

static uint32_t alloc_data(struct ext_sb *s, uint8_t *in, uint32_t goal)
{
    uint32_t v = ext_alloc_block_goal(s, goal);
    if (v)
        ext_inode_add_blocks(s, in, 1);
    return v;
}

static uint32_t alloc_index(struct ext_sb *s, uint8_t *in)
{
    uint32_t v = alloc_data(s, in, 0);
    if (!v)
        return 0;
    uint8_t *z = kmalloc(s->bs);
    if (!z) {
        ext_free_block(s, v);
        ext_inode_add_blocks(s, in, -1);
        return 0;
    }
    ext_write_blk(s, v, z);
    kfree(z);
    return v;
}

uint32_t ext_get_iblock(struct ext_sb *s, uint32_t ino, uint8_t *in, uint32_t b, int alloc)
{
    uint32_t flags = rd32(in + 32);
    if (flags & EXT4_EXTENTS_FL) {
        if (alloc)
            return ext4_extent_alloc(s, ino, in, b);
        return ext4_extent_lookup(s, in, b);
    }

    uint64_t pb = s->bs / 4;
    uint64_t bb = b;
    int lvl;
    uint32_t idx[3];
    if (bb < 12) {
        lvl = 0;
        idx[0] = bb;
    } else if ((bb -= 12) < pb) {
        lvl = 1;
        idx[0] = bb;
    } else if ((bb -= pb) < pb * pb) {
        lvl = 2;
        idx[0] = bb / pb;
        idx[1] = bb % pb;
    } else if ((bb -= pb * pb) < pb * pb * pb) {
        lvl = 3;
        idx[0] = bb / (pb * pb);
        idx[1] = (bb / pb) % pb;
        idx[2] = bb % pb;
    } else {
        return 0;
    }

    uint8_t *p = lvl == 0 ? in + 40 + idx[0] * 4 : in + 88 + (lvl - 1) * 4;
    uint32_t cur = rd32(p);
    if (lvl == 0) {
        if (!cur && alloc) {
            uint32_t prev = b ? rd32(in + 40 + (b - 1) * 4) : 0;
            cur = alloc_data(s, in, prev ? prev + 1 : 0);
            if (cur)
                wr32(p, cur);
        }
        return cur;
    }
    if (!cur) {
        if (!alloc)
            return 0;
        cur = alloc_index(s, in);
        if (!cur)
            return 0;
        wr32(p, cur);
    }
    uint8_t *buf = kmalloc(s->bs);
    if (!buf)
        return 0;
    for (int k = 0; k < lvl; k++) {
        if (ext_read_blk(s, cur, buf)) {
            kfree(buf);
            return 0;
        }
        uint32_t ent = rd32(buf + idx[k] * 4);
        if (!ent) {
            if (!alloc) {
                kfree(buf);
                return 0;
            }
            ent = (k == lvl - 1) ? alloc_data(s, in, 0) : alloc_index(s, in);
            if (!ent) {
                kfree(buf);
                return 0;
            }
            wr32(buf + idx[k] * 4, ent);
            ext_write_blk(s, cur, buf);
        }
        cur = ent;
    }
    kfree(buf);
    return cur;
}

static int trunc_tree(struct ext_sb *s, uint8_t *in, uint32_t blk, int level, uint64_t base,
                      uint32_t keep)
{
    uint64_t pb = s->bs / 4;
    uint64_t span = 1;
    for (int i = 1; i < level; i++)
        span *= pb;
    uint8_t *buf = kmalloc(s->bs);
    if (!buf)
        return 0;
    if (ext_read_blk(s, blk, buf)) {
        kfree(buf);
        return 0;
    }
    int changed = 0, remaining = 0;
    for (uint64_t i = 0; i < pb; i++) {
        uint32_t ent = rd32(buf + i * 4);
        if (!ent)
            continue;
        uint64_t ebase = base + i * span;
        if (level == 1) {
            if (ebase >= keep) {
                ext_free_block(s, ent);
                ext_inode_add_blocks(s, in, -1);
                wr32(buf + i * 4, 0);
                changed = 1;
            } else {
                remaining = 1;
            }
        } else {
            if (ebase + span <= keep) {
                remaining = 1;
                continue;
            }
            if (trunc_tree(s, in, ent, level - 1, ebase, keep)) {
                wr32(buf + i * 4, 0);
                changed = 1;
            } else {
                remaining = 1;
            }
        }
    }
    int freed = 0;
    if (!remaining) {
        ext_free_block(s, blk);
        ext_inode_add_blocks(s, in, -1);
        freed = 1;
    } else if (changed) {
        ext_write_blk(s, blk, buf);
    }
    kfree(buf);
    return freed;
}

int ext_free_inode_data(struct ext_sb *s, uint32_t ino, uint8_t *in, uint64_t new_size)
{
    uint32_t keep = (uint32_t)((new_size + s->bs - 1) / s->bs);
    uint32_t flags = rd32(in + 32);
    if (flags & EXT4_EXTENTS_FL)
        return ext4_extent_truncate(s, ino, in, keep);

    for (uint32_t i = 0; i < 12; i++) {
        uint32_t v = rd32(in + 40 + i * 4);
        if (v && i >= keep) {
            ext_free_block(s, v);
            ext_inode_add_blocks(s, in, -1);
            wr32(in + 40 + i * 4, 0);
        }
    }
    uint64_t pb = s->bs / 4;
    uint64_t base = 12;
    uint64_t span = pb;
    for (int lvl = 1; lvl <= 3; lvl++) {
        uint32_t v = rd32(in + 88 + (lvl - 1) * 4);
        if (v && base + span > keep) {
            if (trunc_tree(s, in, v, lvl, base, keep))
                wr32(in + 88 + (lvl - 1) * 4, 0);
        }
        base += span;
        span *= pb;
    }
    return 0;
}

int ext_read_link_target(struct ext_sb *s, uint32_t ino, uint8_t *in, char *out, size_t max)
{
    if (ext_read_inode(s, ino, in))
        return -1;
    uint16_t mode = in[0] | (in[1] << 8);
    if ((mode & EXT_S_IFMT) != EXT_S_IFLNK)
        return -1;
    uint32_t size = rd32(in + 4);
    if (size >= max)
        size = max - 1;
    if (size < 60 && !ext_inode_has_blocks(s, in)) {
        memcpy(out, in + 40, size);
        out[size] = 0;
        return size;
    }
    uint32_t blk = ext_get_iblock(s, ino, in, 0, 0);
    if (!blk)
        return -1;
    uint8_t *tmp = kmalloc(s->bs);
    if (!tmp)
        return -1;
    if (ext_read_blk(s, blk, tmp)) {
        kfree(tmp);
        return -1;
    }
    if (size > s->bs)
        size = s->bs;
    memcpy(out, tmp, size);
    out[size] = 0;
    kfree(tmp);
    return size;
}

int ext_inode_is_fast_symlink(const uint8_t *in)
{
    uint16_t mode = in[0] | (in[1] << 8);
    if ((mode & EXT_S_IFMT) != EXT_S_IFLNK)
        return 0;
    return rd32(in + 4) < 60 && rd32(in + 28) == 0;
}
