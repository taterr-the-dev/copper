#include "ext.h"
#include <kernel/syscall.h>
#include <kernel/kmalloc.h>

struct ext_priv {
    int refs;
    uint32_t ino;
};

int ext_open(void *sbp, const char *path, struct fs_file *f)
{
    struct ext_sb *s = sbp;
    uint32_t ino = ext_resolve_with_symlinks(s, path, 0);
    if (!ino)
        return -1;

    uint8_t in[EXT_INODE_MAX];
    if (ext_read_inode(s, ino, in))
        return -1;

    struct ext_priv *p = kmalloc(sizeof(struct ext_priv));
    if (!p)
        return -1;
    p->refs = 1;
    p->ino = ino;
    f->priv = p;

    f->mode = ext_rd16(in);
    f->size = ext_isize(in);
    f->uid = ext_rd16(in + 2) | ((uint32_t)ext_rd16(in + 0x78) << 16);
    f->gid = ext_rd16(in + 24) | ((uint32_t)ext_rd16(in + 0x7A) << 16);
    f->nlink = ext_rd16(in + 26);
    return 0;
}

int ext_close(struct fs_file *f)
{
    if (f->priv) {
        struct ext_priv *p = f->priv;
        p->refs--;
        if (p->refs == 0)
            kfree(p);
        f->priv = NULL;
    }
    return 0;
}

int64_t ext_read(struct fs_file *f, void *out, size_t len)
{
    struct ext_sb *s = f->sb;
    struct ext_priv *p = (struct ext_priv *)f->priv;
    uint32_t ino = p->ino;

    uint8_t in[EXT_INODE_MAX];
    if (ext_read_inode(s, ino, in))
        return -EIO;

    uint64_t size = ext_isize(in);
    uint64_t pos = f->pos;
    if (pos >= size)
        return 0;

    size_t to_read = len;
    if (pos + to_read > size)
        to_read = size - pos;

    uint8_t *dst = (uint8_t *)out;
    size_t bytes_read = 0;
    uint8_t bb[4096];

    while (bytes_read < to_read) {
        uint32_t blk_idx = (pos + bytes_read) / s->bs;
        uint32_t off = (pos + bytes_read) % s->bs;
        uint32_t blk = ext_get_iblock(s, ino, in, blk_idx, 0);

        size_t chunk = s->bs - off;
        if (chunk > to_read - bytes_read)
            chunk = to_read - bytes_read;

        if (!blk) {
            memset(dst + bytes_read, 0, chunk);
        } else {
            if (ext_read_blk(s, blk, bb))
                return bytes_read ? (int64_t)bytes_read : -EIO;
            memcpy(dst + bytes_read, bb + off, chunk);
        }
        bytes_read += chunk;
    }

    f->pos += bytes_read;
    return bytes_read;
}

int64_t ext_write(struct fs_file *f, const void *buf, size_t len)
{
    struct ext_sb *s = f->sb;
    struct ext_priv *p = (struct ext_priv *)f->priv;
    uint32_t ino = p->ino;
    uint8_t in[EXT_INODE_MAX];
    if (ext_read_inode(s, ino, in))
        return -EIO;

    uint64_t size = ext_isize(in);
    uint64_t pos = f->pos;
    const uint8_t *src = buf;
    size_t written = 0;
    int modified = 0;
    uint8_t bb[4096];

    while (written < len) {
        uint32_t blk_idx = (pos + written) / s->bs;
        uint32_t off = (pos + written) % s->bs;

        int isnew = 0;
        uint32_t blk = ext_get_iblock(s, ino, in, blk_idx, 0);
        if (!blk) {
            blk = ext_get_iblock(s, ino, in, blk_idx, 1);
            isnew = 1;
            modified = 1;
        }
        if (!blk)
            break;

        size_t to_write = s->bs - off;
        if (to_write > len - written)
            to_write = len - written;

        if (to_write < s->bs) {
            if (isnew)
                memset(bb, 0, s->bs);
            else if (ext_read_blk(s, blk, bb))
                break;
        }
        memcpy(bb + off, src + written, to_write);
        if (ext_write_blk(s, blk, bb))
            break;
        written += to_write;
    }

    uint64_t new_size = pos + written;
    if (written > 0 && new_size > size) {
        ext_set_isize(in, new_size);
        modified = 1;
    }
    if (modified)
        ext_write_inode(s, ino, in);

    f->pos += written;
    if (f->pos > f->size)
        f->size = f->pos;
    if (written == 0 && len > 0)
        return -ENOSPC;
    return written;
}

int ext_readlink(void *sbp, const char *path, char *buf, size_t bufsz)
{
    struct ext_sb *s = sbp;
    uint32_t ino = ext_resolve(s, path);
    if (!ino)
        return -ENOENT;
    if (bufsz == 0)
        return -EINVAL;

    uint8_t in[EXT_INODE_MAX];
    if (ext_read_inode(s, ino, in))
        return -EIO;
    if ((ext_rd16(in) & EXT_S_IFMT) != EXT_S_IFLNK)
        return -EINVAL;
    int n = ext_read_link_target(s, ino, in, buf, bufsz);
    if (n < 0)
        return -EIO;
    return n;
}

int ext_truncate(struct fs_file *f, uint64_t length)
{
    struct ext_sb *s = f->sb;
    struct ext_priv *p = (struct ext_priv *)f->priv;
    if (!p)
        return -EINVAL;
    uint8_t in[EXT_INODE_MAX];
    if (ext_read_inode(s, p->ino, in))
        return -EIO;
    uint16_t mode = ext_rd16(in);
    if ((mode & EXT_S_IFMT) == EXT_S_IFDIR)
        return -EISDIR;
    if ((mode & EXT_S_IFMT) != EXT_S_IFREG)
        return -EINVAL;

    uint64_t old = ext_isize(in);
    if (length < old) {
        uint32_t off = length % s->bs;
        if (off) {
            uint32_t blk = ext_get_iblock(s, p->ino, in, length / s->bs, 0);
            if (blk) {
                uint8_t bb[4096];
                if (ext_read_blk(s, blk, bb) == 0) {
                    memset(bb + off, 0, s->bs - off);
                    ext_write_blk(s, blk, bb);
                }
            }
        }
        ext_free_inode_data(s, p->ino, in, length);
    }
    ext_set_isize(in, length);
    ext_write_inode(s, p->ino, in);
    f->size = length;
    return 0;
}
