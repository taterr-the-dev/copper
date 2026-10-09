#include "ext.h"
#include <kernel/syscall.h>
#include <kernel/kmalloc.h>

extern void put_u64(uint64_t v);
extern int rb(struct ext_sb *s, uint64_t o, void *b, size_t l);
extern int wb(struct ext_sb *s, uint64_t o, const void *b, size_t l);

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
        
    uint8_t in[256];
    ext_read_inode(s, ino, in);

    uint16_t mode = in[0] | (in[1] << 8);
    f->mode = mode;
    
    uint32_t *ino_ptr = kmalloc(sizeof(uint32_t));
    if (!ino_ptr)
        return -1;
    *ino_ptr = ino;
    
    struct ext_priv *p = kmalloc(sizeof(struct ext_priv));
    p->refs = 1;
    p->ino = ino;
    f->priv = p;
    
    uint32_t sz;
    memcpy(&sz, in + 4, 4);
    f->size = sz;

    f->uid = in[20] | (in[21] << 8) | (in[22] << 16) | (in[23] << 24);
    f->gid = in[24] | (in[25] << 8) | (in[26] << 16) | (in[27] << 24);

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

    uint8_t in[256];
    ext_read_inode(s, ino, in);

    uint32_t size;
    memcpy(&size, in + 4, 4);

    uint32_t pos = f->pos;
    if (pos >= size)
        return 0;

    size_t to_read = len;
    if (pos + to_read > size)
        to_read = size - pos;

    uint8_t *dst = (uint8_t *)out;
    size_t bytes_read = 0;

    while (bytes_read < to_read) {
        uint32_t blk_idx = (pos + bytes_read) / s->bs;
        uint32_t off = (pos + bytes_read) % s->bs;

        uint32_t blk = ext_get_iblock(s, in, blk_idx, 0);

        size_t chunk = s->bs - off;
        if (chunk > to_read - bytes_read)
            chunk = to_read - bytes_read;

        if (!blk) {
            memset(dst + bytes_read, 0, chunk);
        } else {
            uint8_t bb[8192];
            ext_read_blk(s, blk, bb);
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
    uint8_t in[256];
    ext_read_inode(s, ino, in);

    uint32_t size;
    memcpy(&size, in + 4, 4);

    uint32_t pos = f->pos;
    const uint8_t *src = buf;
    size_t written = 0;

    while (written < len) {
        uint32_t blk_idx = (pos + written) / s->bs;
        uint32_t off = (pos + written) % s->bs;

        uint32_t blk = ext_get_iblock(s, in, blk_idx, 1);
        if (!blk)
            return -ENOSPC;

        uint8_t bb[8192];
        if (off > 0 || (len - written) < s->bs) {
            ext_read_blk(s, blk, bb);
        } else {
            memset(bb, 0, s->bs);
        }

        size_t to_write = s->bs - off;
        if (to_write > len - written)
            to_write = len - written;

        memcpy(bb + off, src + written, to_write);
        ext_write_blk(s, blk, bb);

        written += to_write;
    }

  uint32_t new_size = pos + written;
  if (new_size > size) {
    memcpy(in + 4, &new_size, 4);
    uint32_t blocks = (new_size + 511) / 512;
    memcpy(in + 28, &blocks, 4);
  }

  if (written > 0) {
    ext_write_inode(s, ino, in);
  }

  f->pos += written;
  return written;
}

int ext_readlink(void *sbp, const char *path, char *buf, size_t bufsz) {
    struct ext_sb *s = sbp;
    uint32_t ino = ext_resolve(s, path);
    if (!ino) return -ENOENT;
    
    uint8_t in[256];
    ext_read_inode(s, ino, in);
    
    uint16_t mode;
    memcpy(&mode, in + 0, 2);
    
    if ((mode & 0xF000) != 0xA000) return -EINVAL;
    
    uint32_t size;
    memcpy(&size, in + 4, 4);
    
    if (size >= bufsz) size = bufsz - 1;
    
    if (size <= 60) {
        memcpy(buf, in + 40, size);
        buf[size] = 0;
    } else {
        uint32_t blk;
        memcpy(&blk, in + 40, 4);
        
        uint8_t *tmp = kmalloc(s->bs);
        if (!tmp) return -ENOMEM;
        
        rb(s, (uint64_t)blk * s->bs, tmp, s->bs);
        memcpy(buf, tmp, size);
        buf[size] = 0;
        
        kfree(tmp);
    }
    
    return size;
}

int ext_truncate(struct fs_file *f, uint64_t length) {
    struct ext_sb *s = f->sb;
    struct ext_priv *p = (struct ext_priv *)f->priv;
    if (!p) return -1;
    uint8_t in[256];
    ext_read_inode(s, p->ino, in);
    uint32_t sz = (uint32_t)length;
    memcpy(in + 4, &sz, 4);
    uint32_t blocks = (sz + 511) / 512;
    memcpy(in + 28, &blocks, 4);
    ext_write_inode(s, p->ino, in);
    f->size = length;
    return 0;
}
