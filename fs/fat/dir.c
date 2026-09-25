#include "fat.h"
#include <kernel/blkdev.h>
#include <kernel/syscall.h>

static void fat_name(const uint8_t *r, char *o)
{
    int i, n = 0;
    for (i = 0; i < 8; i++)
        if (r[i] != ' ')
            o[n++] = r[i];
    if (r[8] != ' ') {
        o[n++] = '.';
        for (i = 8; i < 11; i++)
            if (r[i] != ' ')
                o[n++] = r[i];
    }
    o[n] = 0;
}

static int dir_find(struct fat_sb *s, uint32_t cl, uint32_t rsec, uint32_t rsecs, const char *name,
                    uint8_t *isdir, uint32_t *size, uint32_t *fcl, uint64_t *eoff)
{
    uint32_t c = cl;
    uint32_t sec_idx = 0;

    while ((c >= 2 && c < 0x0FFFFFF8) || (rsec > 0 && sec_idx < rsecs)) {
        if (rsec > 0) {
            fat_read_sector(s, rsec + sec_idx, fat_sec_buf, 1);
            sec_idx++;
        } else {
            fat_read_cl(s, c, fat_sec_buf);
        }

        int per = s->bps / 32;
        for (int i = 0; i < per; i++) {
            uint8_t *e = fat_sec_buf + i * 32;
            if (e[0] == 0)
                return 0;
            if (e[0] == 0xE5 || e[0] == 0x2E || e[11] == 0x0F)
                continue;

            char nm[16];
            fat_name(e, nm);
            if (!strcasecmp(nm, name)) {
                *isdir = (e[11] & 0x10) != 0;
                *size = e[28] | (e[29] << 8) | (e[30] << 16) | ((uint32_t)e[31] << 24);
                *fcl = (e[20] << 16) | (e[21] << 24) | e[26] | (e[27] << 8);

                if (rsec > 0) {
                    *eoff = (uint64_t)(rsec + sec_idx - 1) * s->bps + i * 32;
                } else {
                    *eoff = (uint64_t)fat_cl_sec(s, c) * s->bps + i * 32;
                }
                return 1;
            }
        }
        if (rsec == 0)
            c = fat_next_cl(s, c);
    }
    return 0;
}

int fat_resolve(struct fat_sb *s, const char *path, uint8_t *isdir, uint32_t *size, uint32_t *fcl,
                uint64_t *eoff, uint32_t *rsec, uint32_t *rsecs)
{
    uint32_t cl = s->rootcl;
    uint32_t rs = s->root_sec;
    uint32_t rsecs_val = (rs > 0) ? (s->root_ents * 32 + s->bps - 1) / s->bps : 0;

    *fcl = cl;
    *isdir = 1;
    *size = 0;
    *eoff = 0;
    *rsec = rs;
    *rsecs = rsecs_val;

    const char *p = path;
    while (*p == '/')
        p++;
    if (!*p)
        return 1;

    char part[128];
    while (*p) {
        int i = 0;
        while (p[i] && p[i] != '/')
            i++;
        memcpy(part, p, i);
        part[i] = 0;

        if (part[0] == '.' && part[1] == '\0') {
            p += i;
            while (*p == '/')
                p++;
            continue;
        }
        uint8_t d;
        uint32_t sz, ncl;
        uint64_t eo;
        if (!dir_find(s, cl, rs, rsecs_val, part, &d, &sz, &ncl, &eo))
            return 0;

        cl = ncl;
        rs = 0;
        rsecs_val = 0;

        *fcl = cl;
        *isdir = d;
        *size = sz;
        *eoff = eo;
        *rsec = rs;
        *rsecs = rsecs_val;
        p += i;
        while (*p == '/')
            p++;
    }
    return 1;
}

static void make83(const char *name, uint8_t *out)
{
    for (int i = 0; i < 11; i++)
        out[i] = ' ';
    int o = 0;
    const char *p = name;
    while (*p && *p != '.' && o < 8) {
        char c = *p++;
        if (c >= 'a' && c <= 'z')
            c -= 32;
        out[o++] = c;
    }
    const char *d = name;
    while (*d && *d != '.')
        d++;
    if (*d) {
        d++;
        o = 8;
        while (*d && o < 11) {
            char c = *d++;
            if (c >= 'a' && c <= 'z')
                c -= 32;
            out[o++] = c;
        }
    }
}

int fat_create(void *sbp, const char *path)
{
    struct fat_sb *s = sbp;
    const char *last = path;
    uint8_t d;
    uint32_t sz, cl, rsec, rsecs;
    uint64_t eo;
    if (fat_resolve(s, path, &d, &sz, &cl, &eo, &rsec, &rsecs)) {
        return -EEXIST;
    }
    for (const char *q = path; *q; q++)
        if (*q == '/')
            last = q + 1;

    char parent[128];
    int plen = last - path;
    if (plen > 0 && path[plen - 1] == '/')
        plen--;
    memcpy(parent, path, plen);
    parent[plen] = 0;

    char leaf[128];
    int i = 0;
    for (i = 0; last[i] && last[i] != '/'; i++)
        leaf[i] = last[i];
    leaf[i] = 0;
    if (!fat_resolve(s, parent[0] ? parent : "/", &d, &sz, &cl, &eo, &rsec, &rsecs) || !d)
        return -1;

    uint32_t c = cl;
    uint32_t sec_idx = 0;

    while ((c >= 2 && c < 0x0FFFFFF8) || (rsec > 0 && sec_idx < rsecs)) {
        if (rsec > 0) {
            fat_read_sector(s, rsec + sec_idx, fat_sec_buf, 1);
            sec_idx++;
        } else {
            fat_read_cl(s, c, fat_sec_buf);
        }

        int per = s->bps / 32;
        for (i = 0; i < per; i++) {
            uint8_t *e = fat_sec_buf + i * 32;
            if (e[0] == 0x00 || e[0] == 0xE5) {
                uint8_t nm[11];
                make83(leaf, nm);
                uint8_t ne[32];
                for (int k = 0; k < 32; k++)
                    ne[k] = 0;
                for (int k = 0; k < 11; k++)
                    ne[k] = nm[k];
                ne[11] = 0x20;

                uint64_t off;
                if (rsec > 0) {
                    off = (uint64_t)(rsec + sec_idx - 1) * s->bps + i * 32;
                } else {
                    off = (uint64_t)fat_cl_sec(s, c) * s->bps + i * 32;
                }

                s->dev->write(s->dev, off, ne, 32);
                if (e[0] == 0x00) {
                    uint8_t z = 0;
                    s->dev->write(s->dev, off + 32, &z, 1);
                }
                return 0;
            }
        }
        if (rsec == 0)
            c = fat_next_cl(s, c);
    }
    return -1;
}

int fat_readdir(void *sbp, const char *path, int idx, struct fs_dirent *out)
{
    struct fat_sb *s = sbp;
    uint8_t d;
    uint32_t sz, cl, rsec, rsecs;
    uint64_t eo;
    if (!fat_resolve(s, path, &d, &sz, &cl, &eo, &rsec, &rsecs) || !d)
        return -1;

    uint32_t c = cl;
    uint32_t sec_idx = 0;
    int n = 0;

    while ((c >= 2 && c < 0x0FFFFFF8) || (rsec > 0 && sec_idx < rsecs)) {
        if (rsec > 0) {
            fat_read_sector(s, rsec + sec_idx, fat_sec_buf, 1);
            sec_idx++;
        } else {
            fat_read_cl(s, c, fat_sec_buf);
        }

        int per = s->bps / 32;
        for (int i = 0; i < per; i++) {
            uint8_t *e = fat_sec_buf + i * 32;
            if (e[0] == 0)
                return -1;
            if (e[0] == 0xE5 || e[0] == 0x2E || e[11] == 0x0F)
                continue;

            if (n++ == idx) {
                fat_name(e, out->name);
                out->is_dir = (e[11] & 0x10) != 0;
                out->size = e[28] | (e[29] << 8) | (e[30] << 16) | ((uint32_t)e[31] << 24);

                out->mode = out->is_dir ? 0755 : 0644;
                out->uid = 0;
                out->gid = 0;

                return 0;
            }
        }
        if (rsec == 0)
            c = fat_next_cl(s, c);
    }
    return -1;
}

int fat_unlink(void *sbp, const char *path)
{
    struct fat_sb *s = sbp;

    const char *last_slash = strrchr(path, '/');
    char parent[256], name[256];

    if (!last_slash) {
        strcpy(parent, "/");
        strcpy(name, path);
    } else {
        size_t plen = last_slash - path;
        if (plen == 0) {
            strcpy(parent, "/");
        } else {
            memcpy(parent, path, plen);
            parent[plen] = 0;
        }
        strcpy(name, last_slash + 1);
    }

    uint8_t d;
    uint32_t sz, cl, rsec, rsecs;
    uint64_t eo;
    if (!fat_resolve(s, parent, &d, &sz, &cl, &eo, &rsec, &rsecs) || !d)
        return -1;

    uint32_t c = cl;
    uint32_t sec_idx = 0;

    while ((c >= 2 && c < 0x0FFFFFF8) || (rsec > 0 && sec_idx < rsecs)) {
        if (rsec > 0) {
            fat_read_sector(s, rsec + sec_idx, fat_sec_buf, 1);
            sec_idx++;
        } else {
            fat_read_cl(s, c, fat_sec_buf);
        }

        int per = s->bps / 32;
        for (int i = 0; i < per; i++) {
            uint8_t *e = fat_sec_buf + i * 32;
            if (e[0] == 0)
                return -ENOENT;
            if (e[0] == 0xE5 || e[0] == 0x2E || e[11] == 0x0F)
                continue;

            char nm[16];
            fat_name(e, nm);

            if (strcasecmp(nm, name) == 0) {
                if (e[11] & 0x10)
                    return -EISDIR;

                e[0] = 0xE5;

                uint64_t off;
                if (rsec > 0) {
                    off = (uint64_t)(rsec + sec_idx - 1) * s->bps + i * 32;
                } else {
                    off = (uint64_t)fat_cl_sec(s, c) * s->bps + i * 32;
                }

                s->dev->write(s->dev, off, e, 32);

                // TODO  free clusters
                uint32_t fcl = (e[20] << 16) | (e[21] << 24) | e[26] | (e[27] << 8);
                if (fcl >= 2) {
                    uint32_t next = fcl;
                    while (next >= 2 && next < 0x0FFFFFF8) {
                        uint32_t n = fat_next_cl(s, next);
                        fat_set_fat(s, next, 0);
                        next = n;
                    }
                }

                return 0;
            }
        }
        if (rsec == 0)
            c = fat_next_cl(s, c);
    }
    return -1;
}

int fat_mkdir(void *sbp, const char *path, uint32_t mode)
{
    struct fat_sb *s = sbp;
    (void)mode;

    const char *last_slash = strrchr(path, '/');
    char parent[256], name[256];

    if (!last_slash) {
        strcpy(parent, "/");
        strcpy(name, path);
    } else {
        size_t plen = last_slash - path;
        if (plen == 0) {
            strcpy(parent, "/");
        } else {
            memcpy(parent, path, plen);
            parent[plen] = 0;
        }
        strcpy(name, last_slash + 1);
    }

    uint8_t d;
    uint32_t sz, cl, rsec, rsecs;
    uint64_t eo;
    if (!fat_resolve(s, parent, &d, &sz, &cl, &eo, &rsec, &rsecs) || !d)
        return -1;

    uint32_t new_cl = fat_alloc_cl(s);
    if (!new_cl)
        return -1;

    uint8_t dir_buf[8192] = {0};

    memcpy(dir_buf, ".          ", 11);
    dir_buf[11] = 0x10;

    memcpy(dir_buf + 32, "..         ", 11);
    dir_buf[32 + 11] = 0x10;

    fat_write_cl(s, new_cl, dir_buf);

    uint32_t c = cl;
    uint32_t sec_idx = 0;

    while ((c >= 2 && c < 0x0FFFFFF8) || (rsec > 0 && sec_idx < rsecs)) {
        if (rsec > 0) {
            fat_read_sector(s, rsec + sec_idx, fat_sec_buf, 1);
            sec_idx++;
        } else {
            fat_read_cl(s, c, fat_sec_buf);
        }

        int per = s->bps / 32;
        for (int i = 0; i < per; i++) {
            uint8_t *e = fat_sec_buf + i * 32;
            if (e[0] == 0x00 || e[0] == 0xE5) {
                uint8_t nm[11];
                make83(name, nm);

                memset(e, 0, 32);
                memcpy(e, nm, 11);
                e[11] = 0x10;
                e[26] = new_cl & 0xff;
                e[27] = (new_cl >> 8) & 0xff;
                e[20] = (new_cl >> 16) & 0xff;
                e[21] = (new_cl >> 24) & 0xff;

                uint64_t off;
                if (rsec > 0) {
                    off = (uint64_t)(rsec + sec_idx - 1) * s->bps + i * 32;
                } else {
                    off = (uint64_t)fat_cl_sec(s, c) * s->bps + i * 32;
                }

                s->dev->write(s->dev, off, e, 32);

                if (e[0] == 0x00) {
                    uint8_t z = 0;
                    s->dev->write(s->dev, off + 32, &z, 1);
                }

                return 0;
            }
        }
        if (rsec == 0)
            c = fat_next_cl(s, c);
    }

    fat_set_fat(s, new_cl, 0);
    return -1;
}

int fat_rmdir(void *sbp, const char *path)
{
    struct fat_sb *s = sbp;

    uint8_t d;
    uint32_t sz, cl, rsec, rsecs;
    uint64_t eo;
    if (!fat_resolve(s, path, &d, &sz, &cl, &eo, &rsec, &rsecs) || !d)
        return -1;

    uint32_t c = cl;
    int entry_count = 0;

    while (c >= 2 && c < 0x0FFFFFF8) {
        fat_read_cl(s, c, fat_sec_buf);

        int per = s->bps / 32;
        for (int i = 0; i < per; i++) {
            uint8_t *e = fat_sec_buf + i * 32;
            if (e[0] == 0)
                break;
            if (e[0] == 0xE5 || e[11] == 0x0F)
                continue;
            if (e[0] == 0x2E)
                continue;

            entry_count++;
        }

        c = fat_next_cl(s, c);
    }

    if (entry_count > 0)
        return -1;

    const char *last_slash = strrchr(path, '/');
    char parent[256], name[256];

    if (!last_slash) {
        strcpy(parent, "/");
        strcpy(name, path);
    } else {
        size_t plen = last_slash - path;
        if (plen == 0) {
            strcpy(parent, "/");
        } else {
            memcpy(parent, path, plen);
            parent[plen] = 0;
        }
        strcpy(name, last_slash + 1);
    }

    if (!fat_resolve(s, parent, &d, &sz, &cl, &eo, &rsec, &rsecs) || !d)
        return -1;

    c = cl;
    uint32_t sec_idx = 0;

    while ((c >= 2 && c < 0x0FFFFFF8) || (rsec > 0 && sec_idx < rsecs)) {
        if (rsec > 0) {
            fat_read_sector(s, rsec + sec_idx, fat_sec_buf, 1);
            sec_idx++;
        } else {
            fat_read_cl(s, c, fat_sec_buf);
        }

        int per = s->bps / 32;
        for (int i = 0; i < per; i++) {
            uint8_t *e = fat_sec_buf + i * 32;
            if (e[0] == 0)
                return -1;
            if (e[0] == 0xE5 || e[0] == 0x2E || e[11] == 0x0F)
                continue;

            char nm[16];
            fat_name(e, nm);

            if (strcasecmp(nm, name) == 0) {
                e[0] = 0xE5;

                uint64_t off;
                if (rsec > 0) {
                    off = (uint64_t)(rsec + sec_idx - 1) * s->bps + i * 32;
                } else {
                    off = (uint64_t)fat_cl_sec(s, c) * s->bps + i * 32;
                }

                s->dev->write(s->dev, off, e, 32);

                uint32_t next = cl;
                while (next >= 2 && next < 0x0FFFFFF8) {
                    uint32_t n = fat_next_cl(s, next);
                    fat_set_fat(s, next, 0);
                    next = n;
                }

                return 0;
            }
        }
        if (rsec == 0)
            c = fat_next_cl(s, c);
    }

    return -1;
}
