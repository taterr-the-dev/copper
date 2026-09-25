#ifndef FS_FAT_H
#define FS_FAT_H

#include <kernel/fs.h>
#include <kernel/string.h>
#include <kernel/console.h>
#include <kernel/kmalloc.h>

extern uint8_t fat_sec_buf[8192];

struct fat_sb {
    struct blkdev *dev;
    uint32_t bps;
    uint32_t spc;
    uint32_t reserved;
    uint32_t nfats;
    uint32_t fatsz;
    uint32_t rootcl;
    uint32_t root_sec;
    uint32_t root_ents;
    uint32_t datasec;
    uint32_t total_cl;
    int fat_type;
};

struct fat_file {
    int refs;
    uint32_t first_cl;
    uint32_t size;
    uint64_t dirent_off;
    uint32_t root_sec;
    uint32_t root_secs;
};

struct fat_file *alloc_fat_file(void);

int fat_init_sb(struct blkdev *dev, void **sbp, int force_type);
uint32_t fat_next_cl(struct fat_sb *s, uint32_t c);
void fat_set_fat(struct fat_sb *s, uint32_t c, uint32_t v);
uint32_t fat_alloc_cl(struct fat_sb *s);
uint64_t fat_cl_sec(struct fat_sb *s, uint32_t c);
int fat_read_cl(struct fat_sb *s, uint32_t c, void *b);
int fat_write_cl(struct fat_sb *s, uint32_t c, const void *b);
int fat_read_sector(struct fat_sb *s, uint64_t sec_, void *b, int n);
int fat_write_sector(struct fat_sb *s, uint64_t sec_, const void *b, int n);

int fat_open(void *sbp, const char *path, struct fs_file *f);
int64_t fat_read(struct fs_file *f, void *buf, size_t len);
int64_t fat_write(struct fs_file *f, const void *buf, size_t len);
int fat_create(void *sbp, const char *path);
int fat_readdir(void *sbp, const char *path, int idx, struct fs_dirent *out);
int fat_unlink(void *sbp, const char *path);
int fat_mkdir(void *sbp, const char *path, uint32_t mode);
int fat_rmdir(void *sbp, const char *path);

int fat12_mount(struct blkdev *dev, void **sbp);
int fat16_mount(struct blkdev *dev, void **sbp);
int fat32_mount(struct blkdev *dev, void **sbp);

#endif
