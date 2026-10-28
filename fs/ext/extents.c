#include "extents.h"
#include <kernel/console.h>

#define EH(p) ((struct ext4_extent_header *)(p))
#define EX(p, i) ((struct ext4_extent *)((uint8_t *)(p) + 12 + (i) * 12))
#define EI(p, i) ((struct ext4_extent_idx *)((uint8_t *)(p) + 12 + (i) * 12))
#define EXT_MAX_DEPTH 5
#define EXT_MAX_LEN 32768

static uint32_t xlen(const struct ext4_extent *e)
{
    return e->ee_len > EXT_MAX_LEN ? e->ee_len - EXT_MAX_LEN : e->ee_len;
}

static int xunwritten(const struct ext4_extent *e)
{
    return e->ee_len > EXT_MAX_LEN;
}

static uint32_t xphys(const struct ext4_extent *e)
{
    return e->ee_start_hi ? 0 : e->ee_start_lo;
}

static uint32_t ichild(const struct ext4_extent_idx *e)
{
    return e->ei_leaf_hi ? 0 : e->ei_leaf_lo;
}

static uint32_t flags_of(const uint8_t *in)
{
    uint32_t f;
    memcpy(&f, in + 32, 4);
    return f;
}

void ext4_extent_init_root(struct ext_sb *s, uint8_t *in)
{
    (void)s;
    memset(in + 40, 0, 60);
    struct ext4_extent_header *eh = EH(in + 40);
    eh->eh_magic = 0xF30A;
    eh->eh_entries = 0;
    eh->eh_max = 4;
    eh->eh_depth = 0;
    eh->eh_generation = 0;
}

static uint32_t find_map(struct ext_sb *s, uint8_t *in, uint32_t lblock, int *unwritten)
{
    if (unwritten)
        *unwritten = 0;
    if (!(flags_of(in) & EXT4_EXTENTS_FL))
        return 0;
    if (EH(in + 40)->eh_magic != 0xF30A)
        return 0;

    uint8_t buf[4096];
    uint8_t *cur = in + 40;
    uint16_t depth = EH(cur)->eh_depth;
    if (depth > EXT_MAX_DEPTH)
        return 0;

    while (depth > 0) {
        struct ext4_extent_header *hdr = EH(cur);
        uint32_t child = 0;
        for (int i = 0; i < hdr->eh_entries; i++) {
            if (lblock >= EI(cur, i)->ei_block)
                child = ichild(EI(cur, i));
            else
                break;
        }
        if (!child)
            return 0;
        if (ext_read_blk(s, child, buf))
            return 0;
        cur = buf;
        if (EH(cur)->eh_magic != 0xF30A)
            return 0;
        depth--;
    }

    struct ext4_extent_header *hdr = EH(cur);
    for (int i = 0; i < hdr->eh_entries; i++) {
        struct ext4_extent *e = EX(cur, i);
        uint32_t len = xlen(e);
        if (lblock >= e->ee_block && lblock - e->ee_block < len) {
            if (xunwritten(e)) {
                if (unwritten)
                    *unwritten = 1;
                return 0;
            }
            uint32_t p = xphys(e);
            return p ? p + (lblock - e->ee_block) : 0;
        }
    }
    return 0;
}

uint32_t ext4_extent_lookup(struct ext_sb *s, uint8_t *in, uint32_t lblock)
{
    return find_map(s, in, lblock, 0);
}

struct xpath {
    uint8_t *buf[EXT_MAX_DEPTH + 1];
    uint32_t blk[EXT_MAX_DEPTH + 1];
    int idx[EXT_MAX_DEPTH + 1];
    int depth;
};

static void path_free(struct xpath *p)
{
    for (int i = 1; i <= EXT_MAX_DEPTH; i++) {
        if (p->buf[i])
            kfree(p->buf[i]);
        p->buf[i] = 0;
    }
}

static int path_walk(struct ext_sb *s, uint8_t *in, uint32_t lblock, struct xpath *p)
{
    memset(p, 0, sizeof(*p));
    p->buf[0] = in + 40;
    p->depth = EH(in + 40)->eh_depth;
    if (p->depth > EXT_MAX_DEPTH)
        return -1;
    for (int l = 0; l < p->depth; l++) {
        struct ext4_extent_header *hdr = EH(p->buf[l]);
        if (hdr->eh_entries == 0)
            return -1;
        int j = 0;
        for (int i = 0; i < hdr->eh_entries; i++) {
            if (lblock >= EI(p->buf[l], i)->ei_block)
                j = i;
            else
                break;
        }
        p->idx[l] = j;
        uint32_t child = ichild(EI(p->buf[l], j));
        if (!child)
            return -1;
        p->buf[l + 1] = kmalloc(s->bs);
        if (!p->buf[l + 1])
            return -1;
        p->blk[l + 1] = child;
        if (ext_read_blk(s, child, p->buf[l + 1]))
            return -1;
        if (EH(p->buf[l + 1])->eh_magic != 0xF30A)
            return -1;
    }
    return 0;
}

static int node_write(struct ext_sb *s, uint32_t ino, uint8_t *in, struct xpath *p, int level)
{
    if (level == 0)
        return 0;
    return ext_write_ext_blk(s, ino, in, p->blk[level], p->buf[level]);
}

static uint32_t node_first_key(uint8_t *node)
{
    struct ext4_extent_header *h = EH(node);
    if (h->eh_entries == 0)
        return 0;
    return h->eh_depth ? EI(node, 0)->ei_block : EX(node, 0)->ee_block;
}

static uint16_t block_max(struct ext_sb *s)
{
    return (s->bs - 12) / 12;
}

static uint32_t new_node_block(struct ext_sb *s, uint8_t *in, uint32_t goal)
{
    uint32_t nb = ext_alloc_block_goal(s, goal);
    if (nb)
        ext_inode_add_blocks(s, in, 1);
    return nb;
}

static int grow_root(struct ext_sb *s, uint32_t ino, uint8_t *in)
{
    uint8_t *root = in + 40;
    uint8_t *buf = kmalloc(s->bs);
    if (!buf)
        return -1;
    uint32_t nb = new_node_block(s, in, 0);
    if (!nb) {
        kfree(buf);
        return -1;
    }
    struct ext4_extent_header *rh = EH(root);
    uint16_t n = rh->eh_entries;
    memcpy(buf, root, 12 + n * 12);
    EH(buf)->eh_max = block_max(s);
    uint32_t key = node_first_key(root);
    if (ext_write_ext_blk(s, ino, in, nb, buf)) {
        kfree(buf);
        return -1;
    }
    kfree(buf);
    rh->eh_depth++;
    rh->eh_entries = 1;
    struct ext4_extent_idx *ei = EI(root, 0);
    ei->ei_block = key;
    ei->ei_leaf_lo = nb;
    ei->ei_leaf_hi = 0;
    ei->ei_unused = 0;
    return 0;
}

static int split_node(struct ext_sb *s, uint32_t ino, uint8_t *in, struct xpath *p, int level,
                      uint32_t lblock, int at_end)
{
    uint8_t *node = p->buf[level];
    struct ext4_extent_header *h = EH(node);
    int leaf = h->eh_depth == 0;
    uint16_t n = h->eh_entries;
    uint16_t keep_n = (leaf && at_end) ? n : n / 2;
    uint16_t move_n = n - keep_n;
    uint8_t *nbuf = kmalloc(s->bs);
    if (!nbuf)
        return -1;
    uint32_t goal = p->blk[level];
    uint32_t nb = new_node_block(s, in, goal ? goal + 1 : 0);
    if (!nb) {
        kfree(nbuf);
        return -1;
    }
    struct ext4_extent_header *nh = EH(nbuf);
    nh->eh_magic = 0xF30A;
    nh->eh_entries = move_n;
    nh->eh_max = block_max(s);
    nh->eh_depth = h->eh_depth;
    nh->eh_generation = 0;
    memcpy(nbuf + 12, node + 12 + keep_n * 12, move_n * 12);
    uint32_t newkey = move_n ? node_first_key(nbuf) : lblock;
    if (ext_write_ext_blk(s, ino, in, nb, nbuf)) {
        kfree(nbuf);
        return -1;
    }
    kfree(nbuf);
    h->eh_entries = keep_n;
    node_write(s, ino, in, p, level);

    uint8_t *par = p->buf[level - 1];
    struct ext4_extent_header *ph = EH(par);
    int at = p->idx[level - 1] + 1;
    memmove(EI(par, at + 1), EI(par, at), (ph->eh_entries - at) * 12);
    struct ext4_extent_idx *ei = EI(par, at);
    ei->ei_block = newkey;
    ei->ei_leaf_lo = nb;
    ei->ei_leaf_hi = 0;
    ei->ei_unused = 0;
    ph->eh_entries++;
    node_write(s, ino, in, p, level - 1);
    return 0;
}

static int make_room(struct ext_sb *s, uint32_t ino, uint8_t *in, struct xpath *p, int level,
                     uint32_t lblock, int at_end)
{
    struct ext4_extent_header *h = EH(p->buf[level]);
    if (h->eh_entries < h->eh_max)
        return 0;
    if (level == 0)
        return grow_root(s, ino, in);
    struct ext4_extent_header *ph = EH(p->buf[level - 1]);
    if (ph->eh_entries >= ph->eh_max)
        return make_room(s, ino, in, p, level - 1, lblock, 0);
    return split_node(s, ino, in, p, level, lblock, at_end);
}

static void fix_keys(struct ext_sb *s, uint32_t ino, uint8_t *in, struct xpath *p, uint32_t lblock)
{
    for (int l = p->depth - 1; l >= 0; l--) {
        struct ext4_extent_idx *ei = EI(p->buf[l], p->idx[l]);
        if (ei->ei_block <= lblock)
            break;
        ei->ei_block = lblock;
        node_write(s, ino, in, p, l);
        if (p->idx[l] != 0)
            break;
    }
}

static int insert_mapping(struct ext_sb *s, uint32_t ino, uint8_t *in, uint32_t lblock,
                          uint32_t pblock)
{
    for (int attempt = 0; attempt < 16; attempt++) {
        struct xpath p;
        if (path_walk(s, in, lblock, &p)) {
            path_free(&p);
            return -1;
        }
        uint8_t *leaf = p.buf[p.depth];
        struct ext4_extent_header *h = EH(leaf);
        int pos = 0;
        while (pos < h->eh_entries && EX(leaf, pos)->ee_block < lblock)
            pos++;

        if (pos > 0) {
            struct ext4_extent *pr = EX(leaf, pos - 1);
            uint32_t pl = xlen(pr);
            if (!xunwritten(pr) && pr->ee_block + pl == lblock && xphys(pr) + pl == pblock &&
                pl < EXT_MAX_LEN) {
                pr->ee_len = pl + 1;
                if (pos < h->eh_entries) {
                    struct ext4_extent *nx = EX(leaf, pos);
                    uint32_t nl = xlen(nx);
                    if (!xunwritten(nx) && nx->ee_block == lblock + 1 && xphys(nx) == pblock + 1 &&
                        pl + 1 + nl <= EXT_MAX_LEN) {
                        pr->ee_len = pl + 1 + nl;
                        memmove(EX(leaf, pos), EX(leaf, pos + 1),
                                (h->eh_entries - pos - 1) * 12);
                        h->eh_entries--;
                    }
                }
                node_write(s, ino, in, &p, p.depth);
                path_free(&p);
                return 0;
            }
        }
        if (pos < h->eh_entries) {
            struct ext4_extent *nx = EX(leaf, pos);
            uint32_t nl = xlen(nx);
            if (!xunwritten(nx) && nx->ee_block == lblock + 1 && xphys(nx) == pblock + 1 &&
                nl < EXT_MAX_LEN) {
                nx->ee_block = lblock;
                nx->ee_start_lo = pblock;
                nx->ee_start_hi = 0;
                nx->ee_len = nl + 1;
                node_write(s, ino, in, &p, p.depth);
                if (pos == 0)
                    fix_keys(s, ino, in, &p, lblock);
                path_free(&p);
                return 0;
            }
        }
        if (h->eh_entries < h->eh_max) {
            memmove(EX(leaf, pos + 1), EX(leaf, pos), (h->eh_entries - pos) * 12);
            struct ext4_extent *ne = EX(leaf, pos);
            ne->ee_block = lblock;
            ne->ee_len = 1;
            ne->ee_start_lo = pblock;
            ne->ee_start_hi = 0;
            h->eh_entries++;
            node_write(s, ino, in, &p, p.depth);
            if (pos == 0)
                fix_keys(s, ino, in, &p, lblock);
            path_free(&p);
            return 0;
        }
        int at_end = pos == h->eh_entries;
        int r = make_room(s, ino, in, &p, p.depth, lblock, at_end);
        path_free(&p);
        if (r)
            return -1;
    }
    return -1;
}

uint32_t ext4_extent_alloc(struct ext_sb *s, uint32_t ino, uint8_t *in, uint32_t lblock)
{
    if (!(flags_of(in) & EXT4_EXTENTS_FL))
        return 0;
    if (EH(in + 40)->eh_magic != 0xF30A)
        ext4_extent_init_root(s, in);

    int unwritten = 0;
    uint32_t cur = find_map(s, in, lblock, &unwritten);
    if (cur)
        return cur;
    if (unwritten)
        return 0;

    uint32_t goal = 0;
    if (lblock > 0) {
        uint32_t prev = find_map(s, in, lblock - 1, 0);
        if (prev)
            goal = prev + 1;
    }
    uint32_t pblock = ext_alloc_block_goal(s, goal);
    if (!pblock)
        return 0;
    ext_inode_add_blocks(s, in, 1);
    if (insert_mapping(s, ino, in, lblock, pblock)) {
        ext_free_block(s, pblock);
        ext_inode_add_blocks(s, in, -1);
        return 0;
    }
    return pblock;
}

static void free_extent_range(struct ext_sb *s, uint8_t *in, uint32_t phys, uint32_t len)
{
    if (!phys || !len)
        return;
    ext_free_range(s, phys, len);
    ext_inode_add_blocks(s, in, -(int)len);
}

static void free_subtree(struct ext_sb *s, uint8_t *in, uint32_t blk, int depth)
{
    uint8_t *buf = kmalloc(s->bs);
    if (!buf)
        return;
    if (ext_read_blk(s, blk, buf) == 0 && EH(buf)->eh_magic == 0xF30A) {
        struct ext4_extent_header *h = EH(buf);
        for (int i = 0; i < h->eh_entries; i++) {
            if (depth == 0)
                free_extent_range(s, in, xphys(EX(buf, i)), xlen(EX(buf, i)));
            else
                free_subtree(s, in, ichild(EI(buf, i)), depth - 1);
        }
    }
    kfree(buf);
    ext_free_block(s, blk);
    ext_inode_add_blocks(s, in, -1);
}

static int trunc_node(struct ext_sb *s, uint32_t ino, uint8_t *in, uint8_t *node, uint32_t keep)
{
    struct ext4_extent_header *h = EH(node);
    if (h->eh_magic != 0xF30A)
        return h->eh_entries;
    if (h->eh_depth == 0) {
        for (int i = h->eh_entries - 1; i >= 0; i--) {
            struct ext4_extent *e = EX(node, i);
            uint32_t len = xlen(e);
            if (e->ee_block >= keep) {
                free_extent_range(s, in, xphys(e), len);
                memmove(EX(node, i), EX(node, i + 1), (h->eh_entries - i - 1) * 12);
                h->eh_entries--;
            } else if (e->ee_block + len > keep) {
                uint32_t nl = keep - e->ee_block;
                free_extent_range(s, in, xphys(e) + nl, len - nl);
                e->ee_len = nl + (xunwritten(e) ? EXT_MAX_LEN : 0);
            }
        }
        return h->eh_entries;
    }
    for (int i = h->eh_entries - 1; i >= 0; i--) {
        struct ext4_extent_idx *ei = EI(node, i);
        uint32_t child = ichild(ei);
        if (ei->ei_block >= keep) {
            if (child)
                free_subtree(s, in, child, h->eh_depth - 1);
            memmove(EI(node, i), EI(node, i + 1), (h->eh_entries - i - 1) * 12);
            h->eh_entries--;
            continue;
        }
        if (i + 1 < h->eh_entries && EI(node, i + 1)->ei_block <= keep)
            continue;
        if (!child)
            continue;
        uint8_t *cb = kmalloc(s->bs);
        if (!cb)
            continue;
        if (ext_read_blk(s, child, cb) == 0) {
            int rem = trunc_node(s, ino, in, cb, keep);
            if (rem == 0) {
                kfree(cb);
                ext_free_block(s, child);
                ext_inode_add_blocks(s, in, -1);
                memmove(EI(node, i), EI(node, i + 1), (h->eh_entries - i - 1) * 12);
                h->eh_entries--;
                continue;
            }
            ext_write_ext_blk(s, ino, in, child, cb);
        }
        kfree(cb);
    }
    return h->eh_entries;
}

int ext4_extent_truncate(struct ext_sb *s, uint32_t ino, uint8_t *in, uint32_t keep)
{
    if (!(flags_of(in) & EXT4_EXTENTS_FL))
        return 0;
    if (EH(in + 40)->eh_magic != 0xF30A)
        return 0;
    int rem = trunc_node(s, ino, in, in + 40, keep);
    struct ext4_extent_header *h = EH(in + 40);
    if (rem == 0 && h->eh_depth > 0)
        h->eh_depth = 0;
    return 0;
}
