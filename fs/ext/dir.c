#include "ext.h"
#include "extents.h"
#include <kernel/syscall.h>
extern void put_u64(uint64_t v);

uint32_t ext_resolve_with_symlinks(struct ext_sb *s, const char *path, int depth) {
    if (depth > 8) return 0; 
    
    uint32_t ino = ext_resolve(s, path);
    if (!ino) return 0;
    
    uint8_t in[256];
    ext_read_inode(s, ino, in);
    
    uint16_t mode;
    memcpy(&mode, in + 0, 2);
    
    if ((mode & 0xF000) == 0xA000) {
        uint32_t size;
        memcpy(&size, in + 4, 4);
        if (size >= 256) size = 255;
        
        char target[256];
        if (size <= 60) {
            memcpy(target, in + 40, size);
        } else {
            uint32_t blk;
            memcpy(&blk, in + 40, 4);
            ext_read_blk(s, blk, target);
        }
        target[size] = 0;
        
        if (target[0] == '/') {
            return ext_resolve_with_symlinks(s, target, depth + 1);
        }
        
        char dir_path[256];
        const char *last_slash = strrchr(path, '/');
        if (last_slash == path) {
            strcpy(dir_path, "/");
        } else if (last_slash) {
            size_t len = last_slash - path;
            memcpy(dir_path, path, len);
            dir_path[len] = 0;
        } else {
            strcpy(dir_path, ".");
        }
        
        char abs_target[512];
        if (strcmp(dir_path, "/") == 0) {
            abs_target[0] = '/';
            strcpy(abs_target + 1, target);
        } else {
            strcpy(abs_target, dir_path);
            strcat(abs_target, "/");
            strcat(abs_target, target);
        }
        
        return ext_resolve_with_symlinks(s, abs_target, depth + 1);
    }
    
    return ino;
}

uint32_t ext_resolve(struct ext_sb *s, const char *path)
{
    uint32_t ino = 2;
    char part[128];
    const char *p = path;
    while (*p == '/')
        p++;
    if (!*p)
        return ino;
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
        uint8_t in[256];
        ext_read_inode(s, ino, in);
        uint32_t size;
        memcpy(&size, in + 4, 4);
        uint16_t mode;
        memcpy(&mode, in + 0, 2);
        uint32_t flags;
        memcpy(&flags, in + 32, 4);
        uint32_t off = 0;
        uint32_t found = 0;
        while (off < size) {
            uint32_t blk = ext_get_iblock(s, in, off / s->bs, 0);
            if (!blk) {
                off += s->bs;
                continue;
            }
            uint8_t bb[8192];
            ext_read_blk(s, blk, bb);
            uint32_t io = off % s->bs;
            while (io < s->bs) {
                uint8_t *e = bb + io;
                uint32_t ein = e[0] | (e[1] << 8) | (e[2] << 16) | ((uint32_t)e[3] << 24);
                uint16_t rec = e[4] | (e[5] << 8);
                uint8_t nl = e[6];
                if (ein) {
                    char nm[256];
                    memcpy(nm, e + 8, nl);
                    nm[nl] = 0;
                    int match = 1;
                    for (int j = 0; j < nl; j++) {
                        char c1 = nm[j], c2 = part[j];
                        if (c1 >= 'A' && c1 <= 'Z')
                            c1 += 32;
                        if (c2 >= 'A' && c2 <= 'Z')
                            c2 += 32;
                        if (c1 != c2) {
                            match = 0;
                            break;
                        }
                    }
                    if (match && nl == i) {
                        found = ein;
                        break;
                    }
                }
                if (!rec)
                    break;
                io += rec;
            }
            if (found)
                break;
            off += s->bs;
        }
        if (!found)
            return 0;
        ino = found;
        p += i;
        while (*p == '/')
            p++;
    }
    return ino;
}

int ext_readdir(void *sbp, const char *path, int idx, struct fs_dirent *out)
{
    struct ext_sb *s = sbp;
    uint32_t ino = ext_resolve(s, path);
    if (!ino)
        return -1;
    uint8_t in[256];
    ext_read_inode(s, ino, in);
    uint32_t size;
    memcpy(&size, in + 4, 4);
    uint32_t off = 0;
    int n = 0;
    while (off < size) {
        uint32_t blk = ext_get_iblock(s, in, off / s->bs, 0);
        if (!blk) {
            off += s->bs;
            continue;
        }
        uint8_t bb[8192];
        ext_read_blk(s, blk, bb);
        uint32_t io = off % s->bs;
        while (io < s->bs) {
            uint8_t *e = bb + io;
            uint32_t ei = e[0] | (e[1] << 8) | (e[2] << 16) | ((uint32_t)e[3] << 24);
            uint16_t rec = e[4] | (e[5] << 8);
            uint8_t nl = e[6];
            uint8_t ft = e[7];
            if (ei && nl) {
                if (n++ == idx) {
                    memcpy(out->name, e + 8, nl);
                    out->name[nl] = 0;
                    out->is_dir = (ft == 2);
                    uint8_t child_in[256];
                    ext_read_inode(s, ei, child_in);
                    uint16_t mode = child_in[0] | (child_in[1] << 8);
                    out->mode = mode & 0xFFF;
                    out->uid = child_in[20] | (child_in[21] << 8) | (child_in[22] << 16) |
                               (child_in[23] << 24);
                    out->gid = child_in[24] | (child_in[25] << 8) | (child_in[26] << 16) |
                               (child_in[27] << 24);
                    return 0;
                }
            }
            if (!rec)
                break;
            io += rec;
        }
        off += s->bs;
    }
    return -1;
}

static int add_dirent(struct ext_sb *s, uint32_t dir, const char *name, uint32_t ino, uint8_t type)
{
    uint8_t din[256];
    ext_read_inode(s, dir, din);
    uint32_t dsize;
    memcpy(&dsize, din + 4, 4);
    uint8_t nl = strlen(name);
    uint16_t need = (8 + nl + 3) & ~3;
    uint32_t off = 0;
    while (off < dsize) {
        uint32_t blk = ext_get_iblock(s, din, off / s->bs, 1);
        if (!blk)
            return -1;
        uint8_t bb[8192];
        ext_read_blk(s, blk, bb);
        uint32_t io = off % s->bs;
        uint32_t last_io = 0;
        uint8_t *last = 0;
        while (io < s->bs) {
            uint8_t *e = bb + io;
            uint16_t rec = e[4] | (e[5] << 8);
            if (!rec)
                break;
            last = e;
            last_io = io;
            io += rec;
        }
        if (last) {
            uint32_t ein = last[0] | (last[1] << 8) | (last[2] << 16) | ((uint32_t)last[3] << 24);
            uint16_t rec = last[4] | (last[5] << 8);
            uint8_t onl = ein ? last[6] : 0;
            uint16_t used = (8 + onl + 3) & ~3;
            if (rec - used >= need) {
                if (ein) {
                    last[4] = used & 0xff;
                    last[5] = (used >> 8) & 0xff;
                    uint8_t *ne = bb + last_io + used;
                    ne[0] = ino & 0xff;
                    ne[1] = (ino >> 8) & 0xff;
                    ne[2] = (ino >> 16) & 0xff;
                    ne[3] = (ino >> 24) & 0xff;
                    uint16_t nrec = rec - used;
                    ne[4] = nrec & 0xff;
                    ne[5] = (nrec >> 8) & 0xff;
                    ne[6] = nl;
                    ne[7] = type;
                    memcpy(ne + 8, name, nl);
                } else {
                    last[0] = ino & 0xff;
                    last[1] = (ino >> 8) & 0xff;
                    last[2] = (ino >> 16) & 0xff;
                    last[3] = (ino >> 24) & 0xff;
                    last[6] = nl;
                    last[7] = type;
                    memcpy(last + 8, name, nl);
                }
                ext_write_blk(s, blk, bb);
                return 0;
            }
        }
        off += s->bs;
    }
    uint32_t new_blk = ext_get_iblock(s, din, off / s->bs, 1);
    if (!new_blk)
        return -1;
    uint8_t bb[8192];
    memset(bb, 0, s->bs);
    uint8_t *e = bb;
    e[0] = ino & 0xff;
    e[1] = (ino >> 8) & 0xff;
    e[2] = (ino >> 16) & 0xff;
    e[3] = (ino >> 24) & 0xff;
    e[4] = need & 0xff;
    e[5] = (need >> 8) & 0xff;
    e[6] = nl;
    e[7] = type;
    memcpy(e + 8, name, nl);
    uint16_t remaining = s->bs - need;
    if (remaining > 0) {
        uint8_t *next_e = bb + need;
        next_e[0] = 0;
        next_e[1] = 0;
        next_e[2] = 0;
        next_e[3] = 0;
        next_e[4] = remaining & 0xff;
        next_e[5] = (remaining >> 8) & 0xff;
        next_e[6] = 0;
        next_e[7] = 0;
    }
    ext_write_blk(s, new_blk, bb);
    uint32_t new_size = off + s->bs;
    memcpy(din + 4, &new_size, 4);
    ext_write_inode(s, dir, din);
    return 0;
}

int ext_create(void *sbp, const char *path)
{
    struct ext_sb *s = sbp;
    const char *last = path;
    if (ext_resolve(s, path) != 0) {
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
    uint32_t dir = ext_resolve(s, parent[0] ? parent : "/");
    if (!dir)
        return -1;
    uint32_t ino = ext_alloc_inode(s);
    if (!ino)
        return -1;
    uint8_t in[256];
    memset(in, 0, s->inode_size);
    uint16_t mode = 0x8000 | 0644;
    memcpy(in, &mode, 2);
    uint16_t links = 1;
    memcpy(in + 26, &links, 2);
    uint8_t sb_buf[1024];
    ext_read_at(s, 1024, sb_buf, 1024);
    uint32_t fic =
        sb_buf[0x60] | (sb_buf[0x61] << 8) | (sb_buf[0x62] << 16) | ((uint32_t)sb_buf[0x63] << 24);

    if (fic & 0x0040) {
        uint32_t flags = EXT4_EXTENTS_FL;
        memcpy(in + 32, &flags, 4);

        struct ext4_extent_header *eh = (struct ext4_extent_header *)(in + 40);
        eh->eh_magic = 0xF30A;
        eh->eh_entries = 0;
        eh->eh_max =
            (s->inode_size - 40 - sizeof(struct ext4_extent_header)) / sizeof(struct ext4_extent);
        eh->eh_depth = 0;
        eh->eh_generation = 0;
    }
    return add_dirent(s, dir, last, ino, 1);
}

static int ext_remove_dirent(struct ext_sb *s, uint32_t dir_ino, const char *name)
{
    uint8_t in[256];
    ext_read_inode(s, dir_ino, in);
    uint32_t size;
    memcpy(&size, in + 4, 4);
    uint32_t off = 0;
    while (off < size) {
        uint32_t blk = ext_get_iblock(s, in, off / s->bs, 0);
        if (!blk) {
            off += s->bs;
            continue;
        }
        uint8_t bb[8192];
        ext_read_blk(s, blk, bb);
        uint32_t io = off % s->bs;
        while (io < s->bs) {
            uint8_t *e = bb + io;
            uint32_t ei = e[0] | (e[1] << 8) | (e[2] << 16) | ((uint32_t)e[3] << 24);
            uint16_t rec = e[4] | (e[5] << 8);
            uint8_t nl = e[6];
            if (ei && nl) {
                char nm[256];
                memcpy(nm, e + 8, nl);
                nm[nl] = 0;
                if (strcasecmp(nm, name) == 0) {
                    e[0] = e[1] = e[2] = e[3] = 0;
                    ext_write_blk(s, blk, bb);
                    return 0;
                }
            }
            if (!rec)
                break;
            io += rec;
        }
        off += s->bs;
    }
    return -1;
}

void ext_free_block(struct ext_sb *s, uint32_t blk)
{
    if (!blk)
        return;

    uint8_t sb_buf[1024];
    ext_read_at(s, 1024, sb_buf, 1024);
    uint32_t first_data_block = *(uint32_t *)(sb_buf + 20);
    uint32_t bpg = *(uint32_t *)(sb_buf + 32);

    uint32_t bg = (blk - first_data_block) / bpg;
    uint32_t bit = (blk - first_data_block) % bpg;

    uint8_t gd_buf[8192];
    ext_read_blk(s, s->bgdt_block, gd_buf);
    uint8_t *gd = gd_buf + bg * 32;
    uint32_t bm = *(uint32_t *)(gd + 0);
    uint16_t free_in_group = *(uint16_t *)(gd + 12);

    uint8_t bb[8192];
    ext_read_blk(s, bm, bb);
    bb[bit / 8] &= ~(1 << (bit % 8));
    ext_write_blk(s, bm, bb);

    free_in_group++;
    *(uint16_t *)(gd + 12) = free_in_group;
    ext_write_blk(s, s->bgdt_block, gd_buf);

    uint32_t free_total = *(uint32_t *)(sb_buf + 12);
    free_total++;
    *(uint32_t *)(sb_buf + 12) = free_total;
    ext_write_at(s, 1024, sb_buf, 1024);
}

static void free_indirect(struct ext_sb *s, uint32_t blk, int level)
{
    if (!blk)
        return;
    if (level == 0) {
        ext_free_block(s, blk);
        return;
    }
    uint8_t bb[8192];
    ext_read_blk(s, blk, bb);
    uint32_t count = s->bs / 4;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t v;
        memcpy(&v, bb + i * 4, 4);
        free_indirect(s, v, level - 1);
    }
    ext_free_block(s, blk);
}

static void free_inode_blocks(struct ext_sb *s, uint8_t *in)
{
    for (int i = 0; i < 12; i++) {
        uint32_t v;
        memcpy(&v, in + 40 + i * 4, 4);
        if (v)
            ext_free_block(s, v);
    }
    uint32_t ind;
    memcpy(&ind, in + 88, 4);
    if (ind)
        free_indirect(s, ind, 1);
    memcpy(&ind, in + 92, 4);
    if (ind)
        free_indirect(s, ind, 2);
    memcpy(&ind, in + 96, 4);
    if (ind)
        free_indirect(s, ind, 3);
}

int ext_unlink(void *sbp, const char *path)
{
    struct ext_sb *s = sbp;
    const char *last_slash = strrchr(path, '/');
    char parent[256], name[256];
    if (!last_slash) {
        strcpy(parent, ".");
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
    uint32_t parent_ino = ext_resolve(s, parent);
    if (!parent_ino)
        return -1;
    uint8_t in[256];
    ext_read_inode(s, parent_ino, in);
    uint32_t size;
    memcpy(&size, in + 4, 4);
    uint32_t off = 0;
    uint32_t target_ino = 0;
    while (off < size) {
        uint32_t blk = ext_get_iblock(s, in, off / s->bs, 0);
        if (!blk) {
            off += s->bs;
            continue;
        }
        uint8_t bb[8192];
        ext_read_blk(s, blk, bb);
        uint32_t io = off % s->bs;
        while (io < s->bs) {
            uint8_t *e = bb + io;
            uint32_t ei = e[0] | (e[1] << 8) | (e[2] << 16) | ((uint32_t)e[3] << 24);
            uint16_t rec = e[4] | (e[5] << 8);
            uint8_t nl = e[6];
            if (ei && nl) {
                char nm[256];
                memcpy(nm, e + 8, nl);
                nm[nl] = 0;
                if (strcasecmp(nm, name) == 0) {
                    target_ino = ei;
                    break;
                }
            }
            if (!rec)
                break;
            io += rec;
        }
        if (target_ino)
            break;
        off += s->bs;
    }
    if (!target_ino)
        return -ENOENT;
    uint8_t target_in[256];
    ext_read_inode(s, target_ino, target_in);
    uint16_t mode = target_in[0] | (target_in[1] << 8);
    if ((mode & 0xF000) == 0x4000)
        return -EISDIR;
    ext_remove_dirent(s, parent_ino, name);
    free_inode_blocks(s, target_in);
    memset(target_in, 0, s->inode_size);
    ext_write_inode(s, target_ino, target_in);
    return 0;
}

int ext_mkdir(void *sbp, const char *path, uint32_t mode)
{
    struct ext_sb *s = sbp;
    const char *last_slash = strrchr(path, '/');
    char parent[256], name[256];
    if (!last_slash) {
        strcpy(parent, ".");
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
    uint32_t parent_ino = ext_resolve(s, parent);
    if (!parent_ino)
        return -1;
    if (ext_resolve(s, path) != 0)
        return -EEXIST;
    uint32_t new_ino = ext_alloc_inode(s);
    if (!new_ino)
        return -1;
    uint8_t in[256];
    memset(in, 0, s->inode_size);
    uint16_t dir_mode = 0x4000 | (mode & 0xFFF);
    memcpy(in, &dir_mode, 2);
    uint16_t links = 2;
    memcpy(in + 26, &links, 2);
    uint32_t blk = ext_get_iblock(s, in, 0, 1);
    if (!blk)
        return -1;
    uint8_t bb[8192];
    memset(bb, 0, s->bs);
    uint16_t dot_rec = s->bs - 12;
    uint8_t *e = bb;
    e[0] = new_ino & 0xff;
    e[1] = (new_ino >> 8) & 0xff;
    e[2] = (new_ino >> 16) & 0xff;
    e[3] = (new_ino >> 24) & 0xff;
    e[4] = dot_rec & 0xff;
    e[5] = (dot_rec >> 8) & 0xff;
    e[6] = 1;
    e[7] = 2;
    e[8] = '.';
    uint16_t dotdot_rec = 12;
    e = bb + dot_rec;
    e[0] = parent_ino & 0xff;
    e[1] = (parent_ino >> 8) & 0xff;
    e[2] = (parent_ino >> 16) & 0xff;
    e[3] = (parent_ino >> 24) & 0xff;
    e[4] = dotdot_rec & 0xff;
    e[5] = (dotdot_rec >> 8) & 0xff;
    e[6] = 2;
    e[7] = 2;
    e[8] = '.';
    e[9] = '.';
    ext_write_blk(s, blk, bb);
    uint32_t size = s->bs;
    memcpy(in + 4, &size, 4);
    ext_write_inode(s, new_ino, in);
    if (add_dirent(s, parent_ino, name, new_ino, 2) != 0)
        return -1;
    uint8_t pin[256];
    ext_read_inode(s, parent_ino, pin);
    uint16_t plinks;
    memcpy(&plinks, pin + 26, 2);
    plinks++;
    memcpy(pin + 26, &plinks, 2);
    ext_write_inode(s, parent_ino, pin);
    return 0;
}

int ext_rmdir(void *sbp, const char *path)
{
    struct ext_sb *s = sbp;
    uint32_t ino = ext_resolve(s, path);
    if (!ino)
        return -1;
    uint8_t in[256];
    ext_read_inode(s, ino, in);
    uint16_t mode = in[0] | (in[1] << 8);
    if ((mode & 0xF000) != 0x4000)
        return -1;
    uint32_t size;
    memcpy(&size, in + 4, 4);
    uint32_t off = 0;
    int entry_count = 0;
    while (off < size) {
        uint32_t blk = ext_get_iblock(s, in, off / s->bs, 0);
        if (!blk) {
            off += s->bs;
            continue;
        }
        uint8_t bb[8192];
        ext_read_blk(s, blk, bb);
        uint32_t io = off % s->bs;
        while (io < s->bs) {
            uint8_t *e = bb + io;
            uint32_t ei = e[0] | (e[1] << 8) | (e[2] << 16) | ((uint32_t)e[3] << 24);
            uint16_t rec = e[4] | (e[5] << 8);
            if (ei)
                entry_count++;
            if (!rec)
                break;
            io += rec;
        }
        off += s->bs;
    }
    if (entry_count > 2)
        return -1;
    const char *last_slash = strrchr(path, '/');
    char parent[256], name[256];
    if (!last_slash) {
        strcpy(parent, ".");
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
    uint32_t parent_ino = ext_resolve(s, parent);
    ext_remove_dirent(s, parent_ino, name);
    free_inode_blocks(s, in);
    memset(in, 0, s->inode_size);
    ext_write_inode(s, ino, in);
    return 0;
}
