#ifndef FS_EXT_H
#define FS_EXT_H

#include <kernel/fs.h>
#include <kernel/string.h>
#include <kernel/console.h>
#include <kernel/kmalloc.h>

struct ext_sb {
    struct blkdev *dev;
    uint32_t bs;
    uint32_t ipg;
    uint32_t inode_size;
    uint32_t bgdt_block;
    uint32_t bpg;
    uint32_t inodes;
    uint32_t blocks;
    uint32_t desc_size;
};

int ext_init_sb(struct blkdev *dev, void **sbp);

int ext_read_blk(struct ext_sb *s, uint32_t b, void *out);
int ext_write_blk(struct ext_sb *s, uint32_t b, const void *in);
int ext_read_inode(struct ext_sb *s, uint32_t ino, uint8_t *raw);
int ext_write_inode(struct ext_sb *s, uint32_t ino, const uint8_t *raw);
int ext_read_at(struct ext_sb *s, uint64_t offset, void *out, size_t len);
int ext_write_at(struct ext_sb *s, uint64_t offset, const void *in, size_t len);
uint32_t ext_alloc_block(struct ext_sb *s);
uint32_t ext_alloc_inode(struct ext_sb *s);
uint32_t ext_get_iblock(struct ext_sb *s, uint8_t *in, uint32_t b, int alloc);
uint32_t ext_resolve(struct ext_sb *s, const char *path);
uint32_t ext_resolve_with_symlinks(struct ext_sb *s, const char *path, int depth);

int ext_open(void *sbp, const char *path, struct fs_file *f);
int ext_close(struct fs_file *f);
int64_t ext_read(struct fs_file *f, void *out, size_t len);
int64_t ext_write(struct fs_file *f, const void *buf, size_t len);
int ext_create(void *sbp, const char *path);
int ext_readdir(void *sbp, const char *path, int idx, struct fs_dirent *out);
int ext_unlink(void *sbp, const char *path);
int ext_mkdir(void *sbp, const char *path, uint32_t mode);
int ext_rmdir(void *sbp, const char *path);
int ext_readlink(void *sbp, const char *path, char *buf, size_t bufsz);
int ext_truncate(struct fs_file *f, uint64_t length);

int ext2_mount(struct blkdev *dev, void **sbp);
int ext3_mount(struct blkdev *dev, void **sbp);
int ext4_mount(struct blkdev *dev, void **sbp);

struct ext4_extent_header {
    uint16_t eh_magic;
    uint16_t eh_entries;
    uint16_t eh_max;
    uint16_t eh_depth;
    uint32_t eh_generation;
};

struct ext4_extent {
    uint32_t ee_block;
    uint16_t ee_len;
    uint16_t ee_start_hi;
    uint32_t ee_start_lo;
};

struct ext4_extent_idx {
    uint32_t ei_block;
    uint32_t ei_leaf_lo;
    uint16_t ei_leaf_hi;
    uint16_t ei_unused;
};

#define EXT4_EXTENTS_FL 0x00080000

#endif
