#include "ext.h"
#include "extents.h"
#include <kernel/syscall.h>

#define PATH_BUF 1024
#define MAX_SYMLINKS 8

static int ent_ok(struct ext_sb *s, const uint8_t *bb, uint32_t io, uint32_t lim)
{
    if (io + 8 > lim)
        return 0;
    uint32_t rec = ext_rd16(bb + io + 4);
    if (rec < 8 || (rec & 3) || io + rec > lim)
        return 0;
    if (8u + bb[io + 6] > rec)
        return 0;
    (void)s;
    return 1;
}

static uint32_t blk_limit(struct ext_sb *s, const uint8_t *bb)
{
    if (s->csum && ext_dir_tail_present(s, bb))
        return s->bs - 12;
    return s->bs;
}

static uint8_t mode_ft(struct ext_sb *s, uint16_t mode)
{
    if (!(s->incompat & EXT_INCOMPAT_FILETYPE))
        return 0;
    switch (mode & EXT_S_IFMT) {
    case EXT_S_IFREG: return 1;
    case EXT_S_IFDIR: return 2;
    case 0x2000: return 3;
    case 0x6000: return 4;
    case 0x1000: return 5;
    case 0xC000: return 6;
    case EXT_S_IFLNK: return 7;
    }
    return 0;
}

static int dir_find(struct ext_sb *s, uint32_t dino, const char *name, size_t nl, uint32_t *out_ino,
                    uint8_t *out_ft)
{
    uint8_t din[EXT_INODE_MAX];
    if (ext_read_inode(s, dino, din))
        return -1;
    uint64_t size = ext_isize(din);
    uint8_t bb[4096];
    for (uint64_t off = 0; off < size; off += s->bs) {
        uint32_t blk = ext_get_iblock(s, dino, din, off / s->bs, 0);
        if (!blk)
            continue;
        if (ext_read_blk(s, blk, bb))
            continue;
        uint32_t lim = blk_limit(s, bb);
        uint32_t io = 0;
        while (ent_ok(s, bb, io, lim)) {
            uint8_t *e = bb + io;
            uint32_t ei = ext_rd32(e);
            uint16_t rec = ext_rd16(e + 4);
            if (ei && e[6] == nl && memcmp(e + 8, name, nl) == 0) {
                *out_ino = ei;
                if (out_ft)
                    *out_ft = e[7];
                return 0;
            }
            io += rec;
        }
    }
    return -1;
}

static uint32_t walk(struct ext_sb *s, const char *path, int follow_last)
{
    if (strlen(path) >= PATH_BUF - 24)
        return 0;
    char rem[PATH_BUF], nrem[PATH_BUF], tgt[PATH_BUF];
    strcpy(rem, path);
    uint32_t cur = 2;
    int links = 0;
    const char *p = rem;
    uint8_t in[EXT_INODE_MAX];

    for (;;) {
        while (*p == '/')
            p++;
        if (!*p)
            return cur;
        size_t i = 0;
        while (p[i] && p[i] != '/')
            i++;
        if (i > 255)
            return 0;
        if (i == 1 && p[0] == '.') {
            p += i;
            continue;
        }
        if (ext_read_inode(s, cur, in))
            return 0;
        if ((ext_rd16(in) & EXT_S_IFMT) != EXT_S_IFDIR)
            return 0;
        uint32_t child;
        if (dir_find(s, cur, p, i, &child, 0))
            return 0;
        const char *after = p + i;
        const char *q = after;
        while (*q == '/')
            q++;
        int last = (*q == 0);
        if (ext_read_inode(s, child, in))
            return 0;
        if ((ext_rd16(in) & EXT_S_IFMT) == EXT_S_IFLNK && (!last || follow_last)) {
            if (++links > MAX_SYMLINKS)
                return 0;
            int n = ext_read_link_target(s, child, in, tgt, PATH_BUF / 2);
            if (n <= 0)
                return 0;
            if (strlen(tgt) + strlen(after) + 2 >= PATH_BUF)
                return 0;
            strcpy(nrem, tgt);
            strcat(nrem, "/");
            strcat(nrem, after);
            strcpy(rem, nrem);
            p = rem;
            if (tgt[0] == '/')
                cur = 2;
            continue;
        }
        cur = child;
        p = after;
    }
}

uint32_t ext_resolve(struct ext_sb *s, const char *path)
{
    return walk(s, path, 0);
}

uint32_t ext_resolve_with_symlinks(struct ext_sb *s, const char *path, int depth)
{
    (void)depth;
    return walk(s, path, 1);
}

static int split_path(const char *path, char *parent, size_t psz, char *name)
{
    size_t len = strlen(path);
    while (len > 1 && path[len - 1] == '/')
        len--;
    size_t idx = len;
    while (idx > 0 && path[idx - 1] != '/')
        idx--;
    size_t nl = len - idx;
    if (nl > 255)
        return -ENAMETOOLONG;
    memcpy(name, path + idx, nl);
    name[nl] = 0;
    size_t pl = idx;
    while (pl > 1 && path[pl - 1] == '/')
        pl--;
    if (pl == 0) {
        parent[0] = '/';
        parent[1] = 0;
        return 0;
    }
    if (pl >= psz)
        return -ENAMETOOLONG;
    memcpy(parent, path, pl);
    parent[pl] = 0;
    return 0;
}

static int resolve_parent(struct ext_sb *s, const char *path, uint32_t *dino, char *name)
{
    char parent[PATH_BUF];
    int r = split_path(path, parent, sizeof(parent), name);
    if (r)
        return r;
    uint32_t d = walk(s, parent, 1);
    if (!d)
        return -ENOENT;
    uint8_t in[EXT_INODE_MAX];
    if (ext_read_inode(s, d, in))
        return -EIO;
    if ((ext_rd16(in) & EXT_S_IFMT) != EXT_S_IFDIR)
        return -ENOTDIR;
    *dino = d;
    return 0;
}

int ext_readdir(void *sbp, const char *path, int idx, struct fs_dirent *out)
{
    struct ext_sb *s = sbp;
    uint32_t ino = ext_resolve_with_symlinks(s, path, 0);
    if (!ino)
        return -1;
    uint8_t in[EXT_INODE_MAX];
    if (ext_read_inode(s, ino, in))
        return -1;
    if ((ext_rd16(in) & EXT_S_IFMT) != EXT_S_IFDIR)
        return -1;
    uint64_t size = ext_isize(in);
    int n = 0;
    uint8_t bb[4096];
    for (uint64_t off = 0; off < size; off += s->bs) {
        uint32_t blk = ext_get_iblock(s, ino, in, off / s->bs, 0);
        if (!blk)
            continue;
        if (ext_read_blk(s, blk, bb))
            continue;
        uint32_t lim = blk_limit(s, bb);
        uint32_t io = 0;
        while (ent_ok(s, bb, io, lim)) {
            uint8_t *e = bb + io;
            uint32_t ei = ext_rd32(e);
            uint16_t rec = ext_rd16(e + 4);
            uint8_t nl = e[6];
            if (ei && nl) {
                if (n++ == idx) {
                    memcpy(out->name, e + 8, nl);
                    out->name[nl] = 0;
                    uint8_t cin[EXT_INODE_MAX];
                    ext_read_inode(s, ei, cin);
                    uint16_t mode = ext_rd16(cin);
                    out->is_dir = (mode & EXT_S_IFMT) == EXT_S_IFDIR;
                    out->mode = mode & 0xFFF;
                    out->size = ext_isize(cin);
                    out->uid = ext_rd16(cin + 2);
                    out->gid = ext_rd16(cin + 24);
                    return 0;
                }
            }
            io += rec;
        }
    }
    return -1;
}

static void set_tail(struct ext_sb *s, uint8_t *bb)
{
    uint8_t *t = bb + s->bs - 12;
    memset(t, 0, 12);
    ext_wr16(t + 4, 12);
    t[7] = 0xDE;
}

static int unindex_dir(struct ext_sb *s, uint32_t dino, uint8_t *din)
{
    uint64_t size = ext_isize(din);
    uint8_t bb[4096];
    if (s->csum) {
        for (uint64_t off = 0; off < size; off += s->bs) {
            uint32_t blk = ext_get_iblock(s, dino, din, off / s->bs, 0);
            if (!blk || ext_read_blk(s, blk, bb))
                continue;
            if (off == 0) {
                if (ext_rd16(bb + 4) != 12)
                    return -EIO;
                memset(bb + 24, 0, s->bs - 24);
                ext_wr16(bb + 12 + 4, s->bs - 24);
                set_tail(s, bb);
                ext_write_dir_blk(s, dino, din, blk, bb);
            } else if (ext_rd32(bb) == 0 && ext_rd16(bb + 4) == s->bs) {
                memset(bb, 0, s->bs);
                ext_wr16(bb + 4, s->bs - 12);
                set_tail(s, bb);
                ext_write_dir_blk(s, dino, din, blk, bb);
            }
        }
    }
    uint32_t flags = ext_rd32(din + 32);
    ext_wr32(din + 32, flags & ~EXT_INDEX_FL);
    return ext_write_inode(s, dino, din);
}

static int add_dirent(struct ext_sb *s, uint32_t dino, const char *name, uint32_t ino, uint8_t type)
{
    uint8_t din[EXT_INODE_MAX];
    if (ext_read_inode(s, dino, din))
        return -EIO;
    if (ext_rd32(din + 32) & EXT_INDEX_FL) {
        int r = unindex_dir(s, dino, din);
        if (r)
            return r;
        ext_read_inode(s, dino, din);
    }
    uint64_t dsize = ext_isize(din);
    size_t nl = strlen(name);
    uint16_t need = (8 + nl + 3) & ~3;
    uint8_t bb[4096];

    for (uint64_t off = 0; off < dsize; off += s->bs) {
        uint32_t blk = ext_get_iblock(s, dino, din, off / s->bs, 0);
        if (!blk || ext_read_blk(s, blk, bb))
            continue;
        uint32_t lim = blk_limit(s, bb);
        uint32_t io = 0;
        while (ent_ok(s, bb, io, lim)) {
            uint8_t *e = bb + io;
            uint32_t ein = ext_rd32(e);
            uint16_t rec = ext_rd16(e + 4);
            uint16_t used = ein ? ((8 + e[6] + 3) & ~3) : 0;
            if (rec - used >= need) {
                uint8_t *ne = e;
                if (ein) {
                    ext_wr16(e + 4, used);
                    ne = e + used;
                    ext_wr16(ne + 4, rec - used);
                }
                ext_wr32(ne, ino);
                ne[6] = nl;
                ne[7] = type;
                memcpy(ne + 8, name, nl);
                return ext_write_dir_blk(s, dino, din, blk, bb) ? -EIO : 0;
            }
            io += rec;
        }
    }

    uint32_t nblk = dsize / s->bs;
    uint32_t blk = ext_get_iblock(s, dino, din, nblk, 1);
    if (!blk) {
        ext_write_inode(s, dino, din);
        return -ENOSPC;
    }
    memset(bb, 0, s->bs);
    uint32_t lim = s->csum ? s->bs - 12 : s->bs;
    ext_wr32(bb, ino);
    ext_wr16(bb + 4, lim);
    bb[6] = nl;
    bb[7] = type;
    memcpy(bb + 8, name, nl);
    if (s->csum)
        set_tail(s, bb);
    int wr = ext_write_dir_blk(s, dino, din, blk, bb);
    ext_set_isize(din, dsize + s->bs);
    ext_write_inode(s, dino, din);
    return wr ? -EIO : 0;
}

static int locate(struct ext_sb *s, uint32_t dino, const char *name, uint32_t *blk_out,
                  uint32_t *io_out, int64_t *prev_out, uint8_t *bb)
{
    uint8_t din[EXT_INODE_MAX];
    if (ext_read_inode(s, dino, din))
        return -EIO;
    uint64_t size = ext_isize(din);
    size_t nl = strlen(name);
    for (uint64_t off = 0; off < size; off += s->bs) {
        uint32_t blk = ext_get_iblock(s, dino, din, off / s->bs, 0);
        if (!blk || ext_read_blk(s, blk, bb))
            continue;
        uint32_t lim = blk_limit(s, bb);
        uint32_t io = 0;
        int64_t prev = -1;
        while (ent_ok(s, bb, io, lim)) {
            uint8_t *e = bb + io;
            uint16_t rec = ext_rd16(e + 4);
            if (ext_rd32(e) && e[6] == nl && memcmp(e + 8, name, nl) == 0) {
                *blk_out = blk;
                *io_out = io;
                *prev_out = prev;
                return 0;
            }
            prev = io;
            io += rec;
        }
    }
    return -ENOENT;
}

static int remove_dirent(struct ext_sb *s, uint32_t dino, const char *name)
{
    uint8_t bb[4096];
    uint32_t blk, io;
    int64_t prev;
    int r = locate(s, dino, name, &blk, &io, &prev, bb);
    if (r)
        return r;
    uint8_t din[EXT_INODE_MAX];
    ext_read_inode(s, dino, din);
    if (prev >= 0) {
        uint16_t prec = ext_rd16(bb + prev + 4);
        uint16_t rec = ext_rd16(bb + io + 4);
        ext_wr16(bb + prev + 4, prec + rec);
    } else {
        ext_wr32(bb + io, 0);
        bb[io + 6] = 0;
    }
    return ext_write_dir_blk(s, dino, din, blk, bb) ? -EIO : 0;
}

static int set_entry(struct ext_sb *s, uint32_t dino, const char *name, uint32_t ino, uint8_t ft)
{
    uint8_t bb[4096];
    uint32_t blk, io;
    int64_t prev;
    int r = locate(s, dino, name, &blk, &io, &prev, bb);
    if (r)
        return r;
    uint8_t din[EXT_INODE_MAX];
    ext_read_inode(s, dino, din);
    ext_wr32(bb + io, ino);
    bb[io + 7] = ft;
    return ext_write_dir_blk(s, dino, din, blk, bb) ? -EIO : 0;
}

static void new_inode(struct ext_sb *s, uint8_t *in, uint16_t mode, uint16_t links)
{
    memset(in, 0, s->inode_size);
    ext_wr16(in, mode);
    ext_wr16(in + 26, links);
    if (s->inode_size > 128)
        ext_wr16(in + 0x80, s->extra_isize);
    if (s->incompat & EXT_INCOMPAT_EXTENTS) {
        ext_wr32(in + 32, EXT4_EXTENTS_FL);
        ext4_extent_init_root(s, in);
    }
}

static void nlink_inc(struct ext_sb *s, uint8_t *in)
{
    uint32_t l = ext_rd16(in + 26);
    if ((s->ro_compat & EXT_RO_DIR_NLINK) && l >= 65000)
        l = 1;
    else if (l < 65535)
        l++;
    ext_wr16(in + 26, l);
}

static void dir_nlink_dec(uint8_t *in)
{
    uint32_t l = ext_rd16(in + 26);
    if (l > 2)
        l--;
    ext_wr16(in + 26, l);
}

static int frees_blocks(const uint8_t *in)
{
    uint16_t m = ext_rd16(in) & EXT_S_IFMT;
    if (m == EXT_S_IFREG || m == EXT_S_IFDIR)
        return 1;
    if (m == EXT_S_IFLNK)
        return !ext_inode_is_fast_symlink(in);
    return 0;
}

static void destroy_inode(struct ext_sb *s, uint32_t ino, uint8_t *in)
{
    int was_dir = (ext_rd16(in) & EXT_S_IFMT) == EXT_S_IFDIR;
    if (frees_blocks(in))
        ext_free_inode_data(s, ino, in, 0);
    memset(in, 0, s->inode_size);
    ext_write_inode(s, ino, in);
    ext_free_inode(s, ino, was_dir);
}

int ext_create(void *sbp, const char *path)
{
    struct ext_sb *s = sbp;
    uint32_t dir;
    char name[256];
    int r = resolve_parent(s, path, &dir, name);
    if (r)
        return r;
    if (!name[0])
        return -EEXIST;
    uint32_t ex;
    if (dir_find(s, dir, name, strlen(name), &ex, 0) == 0)
        return -EEXIST;

    uint32_t ino = ext_alloc_inode(s, 0);
    if (!ino)
        return -ENOSPC;
    uint8_t in[EXT_INODE_MAX];
    new_inode(s, in, EXT_S_IFREG | 0644, 1);
    if (ext_write_inode(s, ino, in)) {
        ext_free_inode(s, ino, 0);
        return -EIO;
    }
    r = add_dirent(s, dir, name, ino, mode_ft(s, EXT_S_IFREG));
    if (r) {
        memset(in, 0, s->inode_size);
        ext_write_inode(s, ino, in);
        ext_free_inode(s, ino, 0);
        return r;
    }
    return 0;
}

int ext_unlink(void *sbp, const char *path)
{
    struct ext_sb *s = sbp;
    uint32_t dir;
    char name[256];
    int r = resolve_parent(s, path, &dir, name);
    if (r)
        return r;
    uint32_t ino;
    if (!name[0] || dir_find(s, dir, name, strlen(name), &ino, 0))
        return -ENOENT;
    uint8_t in[EXT_INODE_MAX];
    if (ext_read_inode(s, ino, in))
        return -EIO;
    if ((ext_rd16(in) & EXT_S_IFMT) == EXT_S_IFDIR)
        return -EISDIR;
    r = remove_dirent(s, dir, name);
    if (r)
        return r;
    uint16_t links = ext_rd16(in + 26);
    if (links > 1) {
        ext_wr16(in + 26, links - 1);
        ext_write_inode(s, ino, in);
        return 0;
    }
    destroy_inode(s, ino, in);
    return 0;
}

int ext_mkdir(void *sbp, const char *path, uint32_t mode)
{
    struct ext_sb *s = sbp;
    uint32_t parent;
    char name[256];
    int r = resolve_parent(s, path, &parent, name);
    if (r)
        return r;
    if (!name[0])
        return -EEXIST;
    uint32_t ex;
    if (dir_find(s, parent, name, strlen(name), &ex, 0) == 0)
        return -EEXIST;

    uint32_t ino = ext_alloc_inode(s, 1);
    if (!ino)
        return -ENOSPC;
    uint8_t in[EXT_INODE_MAX];
    new_inode(s, in, EXT_S_IFDIR | (mode & 0xFFF), 2);
    uint32_t blk = ext_get_iblock(s, ino, in, 0, 1);
    if (!blk) {
        memset(in, 0, s->inode_size);
        ext_write_inode(s, ino, in);
        ext_free_inode(s, ino, 1);
        return -ENOSPC;
    }
    uint8_t bb[4096];
    memset(bb, 0, s->bs);
    uint8_t ft = mode_ft(s, EXT_S_IFDIR);
    ext_wr32(bb, ino);
    ext_wr16(bb + 4, 12);
    bb[6] = 1;
    bb[7] = ft;
    bb[8] = '.';
    ext_wr32(bb + 12, parent);
    ext_wr16(bb + 16, (s->csum ? s->bs - 12 : s->bs) - 12);
    bb[18] = 2;
    bb[19] = ft;
    bb[20] = '.';
    bb[21] = '.';
    if (s->csum)
        set_tail(s, bb);
    ext_write_dir_blk(s, ino, in, blk, bb);
    ext_set_isize(in, s->bs);
    ext_write_inode(s, ino, in);

    r = add_dirent(s, parent, name, ino, ft);
    if (r) {
        destroy_inode(s, ino, in);
        return r;
    }
    uint8_t pin[EXT_INODE_MAX];
    ext_read_inode(s, parent, pin);
    nlink_inc(s, pin);
    ext_write_inode(s, parent, pin);
    return 0;
}

static int dir_is_empty(struct ext_sb *s, uint32_t ino, uint8_t *in)
{
    uint64_t size = ext_isize(in);
    uint8_t bb[4096];
    for (uint64_t off = 0; off < size; off += s->bs) {
        uint32_t blk = ext_get_iblock(s, ino, in, off / s->bs, 0);
        if (!blk || ext_read_blk(s, blk, bb))
            continue;
        uint32_t lim = blk_limit(s, bb);
        uint32_t io = 0;
        while (ent_ok(s, bb, io, lim)) {
            uint8_t *e = bb + io;
            uint16_t rec = ext_rd16(e + 4);
            if (ext_rd32(e) && e[6]) {
                int dot = e[6] == 1 && e[8] == '.';
                int dotdot = e[6] == 2 && e[8] == '.' && e[9] == '.';
                if (!dot && !dotdot)
                    return 0;
            }
            io += rec;
        }
    }
    return 1;
}

int ext_rmdir(void *sbp, const char *path)
{
    struct ext_sb *s = sbp;
    uint32_t parent;
    char name[256];
    int r = resolve_parent(s, path, &parent, name);
    if (r)
        return r;
    if (!name[0])
        return -EBUSY;
    if (strcmp(name, ".") == 0)
        return -EINVAL;
    if (strcmp(name, "..") == 0)
        return -ENOTEMPTY;
    uint32_t ino;
    if (dir_find(s, parent, name, strlen(name), &ino, 0))
        return -ENOENT;
    uint8_t in[EXT_INODE_MAX];
    if (ext_read_inode(s, ino, in))
        return -EIO;
    if ((ext_rd16(in) & EXT_S_IFMT) != EXT_S_IFDIR)
        return -ENOTDIR;
    if (!dir_is_empty(s, ino, in))
        return -ENOTEMPTY;
    r = remove_dirent(s, parent, name);
    if (r)
        return r;
    uint8_t pin[EXT_INODE_MAX];
    ext_read_inode(s, parent, pin);
    dir_nlink_dec(pin);
    ext_write_inode(s, parent, pin);
    destroy_inode(s, ino, in);
    return 0;
}

static int is_ancestor(struct ext_sb *s, uint32_t anc, uint32_t dir)
{
    uint32_t cur = dir;
    for (int i = 0; i < 256; i++) {
        if (cur == anc)
            return 1;
        if (cur == 2)
            return 0;
        uint32_t up;
        if (dir_find(s, cur, "..", 2, &up, 0))
            return 0;
        if (up == cur)
            return 0;
        cur = up;
    }
    return 1;
}

int ext_rename(void *sbp, const char *oldpath, const char *newpath)
{
    struct ext_sb *s = sbp;
    uint32_t op, np;
    char oname[256], nname[256];
    int r = resolve_parent(s, oldpath, &op, oname);
    if (r)
        return r;
    r = resolve_parent(s, newpath, &np, nname);
    if (r)
        return r;
    if (!oname[0] || !nname[0])
        return -EBUSY;
    if (!strcmp(oname, ".") || !strcmp(oname, "..") || !strcmp(nname, ".") || !strcmp(nname, ".."))
        return -EINVAL;

    uint32_t oino;
    if (dir_find(s, op, oname, strlen(oname), &oino, 0))
        return -ENOENT;
    uint8_t oin[EXT_INODE_MAX];
    if (ext_read_inode(s, oino, oin))
        return -EIO;
    int old_dir = (ext_rd16(oin) & EXT_S_IFMT) == EXT_S_IFDIR;
    uint8_t ft = mode_ft(s, ext_rd16(oin));

    uint32_t nino = 0;
    int exists = dir_find(s, np, nname, strlen(nname), &nino, 0) == 0;
    uint8_t nin[EXT_INODE_MAX];
    if (exists) {
        if (nino == oino)
            return 0;
        if (ext_read_inode(s, nino, nin))
            return -EIO;
        int new_dir = (ext_rd16(nin) & EXT_S_IFMT) == EXT_S_IFDIR;
        if (old_dir && !new_dir)
            return -ENOTDIR;
        if (!old_dir && new_dir)
            return -EISDIR;
        if (new_dir && !dir_is_empty(s, nino, nin))
            return -ENOTEMPTY;
    }
    if (old_dir && is_ancestor(s, oino, np))
        return -EINVAL;

    if (exists) {
        r = set_entry(s, np, nname, oino, ft);
        if (r)
            return r;
        int new_dir = (ext_rd16(nin) & EXT_S_IFMT) == EXT_S_IFDIR;
        if (new_dir) {
            uint8_t pin[EXT_INODE_MAX];
            ext_read_inode(s, np, pin);
            dir_nlink_dec(pin);
            ext_write_inode(s, np, pin);
            destroy_inode(s, nino, nin);
        } else {
            uint16_t links = ext_rd16(nin + 26);
            if (links > 1) {
                ext_wr16(nin + 26, links - 1);
                ext_write_inode(s, nino, nin);
            } else {
                destroy_inode(s, nino, nin);
            }
        }
    } else {
        r = add_dirent(s, np, nname, oino, ft);
        if (r)
            return r;
    }

    r = remove_dirent(s, op, oname);
    if (r)
        return r;

    if (old_dir && op != np) {
        set_entry(s, oino, "..", np, ft);
        uint8_t pin[EXT_INODE_MAX];
        ext_read_inode(s, op, pin);
        dir_nlink_dec(pin);
        ext_write_inode(s, op, pin);
        ext_read_inode(s, np, pin);
        nlink_inc(s, pin);
        ext_write_inode(s, np, pin);
    }
    return 0;
}
