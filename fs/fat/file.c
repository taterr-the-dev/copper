#include "fat.h"
#include <kernel/blkdev.h>
#include <kernel/syscall.h>

struct fat_file *alloc_fat_file(void)
{
    struct fat_file *ff = kmalloc(sizeof(struct fat_file));
    if (ff) {
        ff->first_cl = 0;
        ff->size = 0;
        ff->dirent_off = 0;
        ff->root_sec = 0;
        ff->root_secs = 0;
    }
    return ff;
}

int fat_open(void *sbp, const char *path, struct fs_file *f)
{
    struct fat_sb *s = sbp;
    uint8_t d;
    uint32_t sz, cl;
    uint64_t eo;

    extern int fat_resolve(struct fat_sb * s, const char *path, uint8_t *isdir, uint32_t *size,
                           uint32_t *fcl, uint64_t *eoff, uint32_t *rsec, uint32_t *rsecs);

    uint32_t rsec = 0, rsecs = 0;
    if (!fat_resolve(s, path, &d, &sz, &cl, &eo, &rsec, &rsecs))
        return -1;

    struct fat_file *ff = alloc_fat_file();
    ff->refs = 1;
    ff->first_cl = cl;
    ff->size = sz;
    ff->dirent_off = eo;
    ff->root_sec = rsec;
    ff->root_secs = rsecs;

    f->priv = ff;
    f->size = sz;

    f->mode = d ? 0040755 : 0100755;
    f->uid = 0;
    f->gid = 0;

    return 0;
}

static uint32_t getcluster(struct fat_sb *s, struct fat_file *ff, uint32_t idx)
{
    if (ff->root_sec > 0)
        return 0;
    if (ff->first_cl < 2) {
        uint32_t n = fat_alloc_cl(s);
        if (!n)
            return 0;
        ff->first_cl = n;
    }
    uint32_t c = ff->first_cl;
    for (uint32_t i = 0; i < idx; i++) {
        uint32_t n = fat_next_cl(s, c);
        if (n < 2 || n >= 0x0FFFFFF8) {
            n = fat_alloc_cl(s);
            if (!n)
                return 0;
            fat_set_fat(s, c, n);
        }
        c = n;
    }
    return c;
}

static void update_dirent(struct fat_sb *s, struct fat_file *ff)
{
    if (!ff->dirent_off || ff->root_sec > 0)
        return;
    uint8_t e[32];
    s->dev->read(s->dev, ff->dirent_off, e, 32);
    e[26] = ff->first_cl & 0xff;
    e[27] = (ff->first_cl >> 8) & 0xff;
    e[20] = (ff->first_cl >> 16) & 0xff;
    e[21] = (ff->first_cl >> 24) & 0xff;
    e[28] = ff->size & 0xff;
    e[29] = (ff->size >> 8) & 0xff;
    e[30] = (ff->size >> 16) & 0xff;
    e[31] = (ff->size >> 24) & 0xff;
    s->dev->write(s->dev, ff->dirent_off, e, 32);
}

int64_t fat_read(struct fs_file *f, void *buf, size_t len)
{
    struct fat_sb *s = f->sb;
    struct fat_file *ff = f->priv;
    uint32_t cb = s->bps * s->spc;
    size_t done = 0;
    uint8_t *o = buf;

    while (done < len && f->pos < f->size) {
        uint32_t cidx = f->pos / cb, off = f->pos % cb;
        uint32_t c = ff->first_cl;

        for (uint32_t i = 0; i < cidx && c >= 2 && c < 0x0FFFFFF8; i++) {
            c = fat_next_cl(s, c);
        }

        if (c < 2 || c >= 0x0FFFFFF8)
            break;

        fat_read_cl(s, c, fat_sec_buf);
        size_t chunk = cb - off;
        if (chunk > len - done)
            chunk = len - done;
        if (f->pos + chunk > f->size)
            chunk = f->size - f->pos;

        memcpy(o + done, fat_sec_buf + off, chunk);
        done += chunk;
        f->pos += chunk;
    }
    return done;
}

int64_t fat_write(struct fs_file *f, const void *buf, size_t len)
{
    struct fat_sb *s = f->sb;
    struct fat_file *ff = f->priv;

    uint32_t pos = f->pos;
    const uint8_t *src = buf;
    size_t written = 0;

    uint32_t cl_size = s->spc * s->bps;

    if (ff->first_cl < 2) {
        ff->first_cl = fat_alloc_cl(s);
        if (!ff->first_cl)
            return -ENOSPC;

        if (ff->dirent_off > 0) {
            uint8_t de[32];
            s->dev->read(s->dev, ff->dirent_off, de, 32);
            de[26] = ff->first_cl & 0xff;
            de[27] = (ff->first_cl >> 8) & 0xff;
            de[20] = (ff->first_cl >> 16) & 0xff;
            de[21] = (ff->first_cl >> 24) & 0xff;
            s->dev->write(s->dev, ff->dirent_off, de, 32);
        }
    }

    while (written < len) {
        uint32_t cl_off = (pos + written) % cl_size;
        uint32_t cl_idx = (pos + written) / cl_size;

        uint32_t c = ff->first_cl;
        for (uint32_t i = 0; i < cl_idx; i++) {
            uint32_t next = fat_next_cl(s, c);
            if (next < 2) {
                next = fat_alloc_cl(s);
                if (!next)
                    return -ENOSPC;
                fat_set_fat(s, c, next);
            }
            c = next;
        }

        uint8_t cb[8192];
        if (cl_off > 0 || (len - written) < cl_size) {
            fat_read_cl(s, c, cb);
        } else {
            memset(cb, 0, cl_size);
        }

        size_t to_write = cl_size - cl_off;
        if (to_write > len - written)
            to_write = len - written;

        memcpy(cb + cl_off, src + written, to_write);
        fat_write_cl(s, c, cb);

        written += to_write;
    }

    uint32_t new_size = pos + written;
    if (new_size > ff->size) {
        ff->size = new_size;
        if (ff->dirent_off > 0) {
            uint8_t de[32];
            s->dev->read(s->dev, ff->dirent_off, de, 32);
            de[28] = new_size & 0xff;
            de[29] = (new_size >> 8) & 0xff;
            de[30] = (new_size >> 16) & 0xff;
            de[31] = (new_size >> 24) & 0xff;
            s->dev->write(s->dev, ff->dirent_off, de, 32);
        }
    }

    f->pos += written;
    return written;
}

int fat_close(struct fs_file *f)
{
    if (f->priv) {
        struct fat_file *ff = (struct fat_file *)f->priv;

        ff->refs--;

        if (ff->refs == 0) {
            kfree(f->priv);
        }

        f->priv = NULL;
    }
    return 0;
}
