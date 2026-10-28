#ifndef FS_EXT_H
#define FS_EXT_H

#include <kernel/fs.h>
#include <kernel/string.h>
#include <kernel/console.h>
#include <kernel/kmalloc.h>
#include <kernel/spinlock.h>

#define EXT_COMPAT_HAS_JOURNAL 0x0004
#define EXT_INCOMPAT_FILETYPE 0x0002
#define EXT_INCOMPAT_JOURNAL_DEV 0x0008
#define EXT_INCOMPAT_META_BG 0x0010
#define EXT_INCOMPAT_EXTENTS 0x0040
#define EXT_INCOMPAT_64BIT 0x0080
#define EXT_INCOMPAT_FLEX_BG 0x0200
#define EXT_INCOMPAT_CSUM_SEED 0x2000
#define EXT_INCOMPAT_INLINE_DATA 0x8000
#define EXT_RO_SPARSE_SUPER 0x0001
#define EXT_RO_HUGE_FILE 0x0008
#define EXT_RO_GDT_CSUM 0x0010
#define EXT_RO_DIR_NLINK 0x0020
#define EXT_RO_EXTRA_ISIZE 0x0040
#define EXT_RO_BIGALLOC 0x0200
#define EXT_RO_METADATA_CSUM 0x0400

#define EXT_BG_INODE_UNINIT 0x1
#define EXT_BG_BLOCK_UNINIT 0x2
#define EXT_BG_ITABLE_ZEROED 0x4

#define EXT4_EXTENTS_FL 0x00080000
#define EXT_INDEX_FL 0x00001000

#define EXT_S_IFMT 0xF000
#define EXT_S_IFDIR 0x4000
#define EXT_S_IFREG 0x8000
#define EXT_S_IFLNK 0xA000

#define EXT_INODE_MAX 256

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
    uint32_t first_data_block;
    uint32_t groups;
    uint32_t compat;
    uint32_t incompat;
    uint32_t ro_compat;
    uint32_t reserved_gdt;
    uint32_t itb_per_group;
    uint32_t csum_seed;
    uint16_t extra_isize;
    int csum;
    int gdt_csum;
    uint8_t uuid[16];
    uint8_t *gdt;
    spinlock_t lock;
};


static inline uint32_t ext_rd32(const uint8_t *p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline uint16_t ext_rd16(const uint8_t *p)
{
    return p[0] | (p[1] << 8);
}

static inline void ext_wr32(uint8_t *p, uint32_t v)
{
    p[0] = v & 0xFF;
    p[1] = (v >> 8) & 0xFF;
    p[2] = (v >> 16) & 0xFF;
    p[3] = (v >> 24) & 0xFF;
}

static inline void ext_wr16(uint8_t *p, uint16_t v)
{
    p[0] = v & 0xFF;
    p[1] = (v >> 8) & 0xFF;
}

static inline uint64_t ext_isize(const uint8_t *in)
{
    uint64_t v = ext_rd32(in + 4);
    if ((ext_rd16(in) & EXT_S_IFMT) == EXT_S_IFREG)
        v |= (uint64_t)ext_rd32(in + 0x6C) << 32;
    return v;
}

static inline void ext_set_isize(uint8_t *in, uint64_t v)
{
    ext_wr32(in + 4, (uint32_t)v);
    if ((ext_rd16(in) & EXT_S_IFMT) == EXT_S_IFREG)
        ext_wr32(in + 0x6C, (uint32_t)(v >> 32));
}

int ext_inode_is_fast_symlink(const uint8_t *in);

int ext_init_sb(struct blkdev *dev, void **sbp);

int rb(struct ext_sb *s, uint64_t o, void *b, size_t l);
int wb(struct ext_sb *s, uint64_t o, const void *b, size_t l);
int ext_read_blk(struct ext_sb *s, uint32_t b, void *out);
int ext_write_blk(struct ext_sb *s, uint32_t b, const void *in);
int ext_read_inode(struct ext_sb *s, uint32_t ino, uint8_t *raw);
int ext_write_inode(struct ext_sb *s, uint32_t ino, const uint8_t *raw);
int ext_read_at(struct ext_sb *s, uint64_t offset, void *out, size_t len);
int ext_write_at(struct ext_sb *s, uint64_t offset, const void *in, size_t len);
uint32_t ext_alloc_block(struct ext_sb *s);
uint32_t ext_alloc_block_goal(struct ext_sb *s, uint32_t goal);
void ext_free_block(struct ext_sb *s, uint32_t blk);
void ext_free_range(struct ext_sb *s, uint32_t start, uint32_t count);
uint32_t ext_alloc_inode(struct ext_sb *s, int is_dir);
void ext_free_inode(struct ext_sb *s, uint32_t ino, int was_dir);
uint32_t ext_get_iblock(struct ext_sb *s, uint32_t ino, uint8_t *in, uint32_t b, int alloc);
int ext_free_inode_data(struct ext_sb *s, uint32_t ino, uint8_t *in, uint64_t new_size);
int ext_inode_has_blocks(struct ext_sb *s, const uint8_t *in);
void ext_inode_add_blocks(struct ext_sb *s, uint8_t *in, int delta);
uint32_t ext_resolve(struct ext_sb *s, const char *path);
uint32_t ext_resolve_with_symlinks(struct ext_sb *s, const char *path, int depth);
int ext_read_link_target(struct ext_sb *s, uint32_t ino, uint8_t *in, char *out, size_t max);

int ext_write_ext_blk(struct ext_sb *s, uint32_t ino, const uint8_t *in, uint32_t blk, uint8_t *buf);
int ext_write_dir_blk(struct ext_sb *s, uint32_t ino, const uint8_t *in, uint32_t blk, uint8_t *buf);

uint32_t ext_crc32c(uint32_t crc, const void *buf, size_t len);
void ext_csum_init(struct ext_sb *s, const uint8_t *sb);
void ext_sb_csum_set(struct ext_sb *s, uint8_t *sb);
void ext_gd_csum_set(struct ext_sb *s, uint32_t g, uint8_t *gd);
void ext_inode_csum_set(struct ext_sb *s, uint32_t ino, uint8_t *in);
uint32_t ext_inode_seed(struct ext_sb *s, uint32_t ino, const uint8_t *in);
void ext_bitmap_csum_set(struct ext_sb *s, uint8_t *gd, const uint8_t *bmp, int inode_bitmap);
void ext_extent_blk_csum_set(struct ext_sb *s, uint32_t ino, const uint8_t *in, uint8_t *buf);
int ext_dir_tail_present(struct ext_sb *s, const uint8_t *buf);
void ext_dir_blk_csum_set(struct ext_sb *s, uint32_t ino, const uint8_t *in, uint8_t *buf);

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
int ext_rename(void *sbp, const char *oldpath, const char *newpath);

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

#endif
