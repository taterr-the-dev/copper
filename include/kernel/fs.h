#ifndef _KERNEL_FS_H
#define _KERNEL_FS_H

#include <kernel/types.h>

struct blkdev;

struct fs_file {
    void *priv;
    void *sb;
    uint64_t pos;
    uint64_t size;
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
    struct fs_ops *ops;
#ifdef CONFIG_TTY
    void *tty;
#endif
};

struct fs_dirent {
    char name[256];
    int is_dir;
    uint64_t size;
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
};

struct fs_ops {
    const char *name;
    int (*mount)(struct blkdev *dev, void **sb);
    int (*open)(void *sb, const char *path, struct fs_file *f);
    int (*close)(struct fs_file *);
    int64_t (*read)(struct fs_file *f, void *buf, size_t len);
    int64_t (*write)(struct fs_file *f, const void *buf, size_t len);
    int (*create)(void *sb, const char *path);
    int (*readdir)(void *sb, const char *path, int idx, struct fs_dirent *out);
    int (*unlink)(void *sb, const char *path);
    int (*mkdir)(void *sb, const char *path, uint32_t mode);
    int (*rmdir)(void *sb, const char *path);
    int (*readlink)(void *sb, const char *path, char *buf, size_t bufsz);
    int (*truncate)(struct fs_file *f, uint64_t length);
    int (*rename)(void *sb, const char *oldpath, const char *newpath);
};

struct mount_point {
    char path[256];
    struct fs_ops *ops;
    void *sb;
    int in_use;
};

void fs_init(void);
void fs_register(struct fs_ops *ops);
int vfs_mount(struct blkdev *dev);
int vfs_mount_at(const char *path, struct fs_ops *ops, void *sb);
int fs_try_mount(struct blkdev *d, struct fs_ops **out_ops, void **out_sb);
int vfs_umount(const char *mount_path);
const char *vfs_name(void);

int vfs_open(const char *path, struct fs_file *f);
int64_t vfs_read(struct fs_file *f, void *buf, size_t len);
int64_t vfs_write(struct fs_file *f, const void *buf, size_t len);
int vfs_truncate(struct fs_file *f, uint64_t length);
int vfs_create(const char *path);
int vfs_readdir(const char *path, int idx, struct fs_dirent *out);
int vfs_close(struct fs_file *f);
int vfs_unlink(const char *path);
int vfs_mkdir(const char *path, uint32_t mode);
int vfs_rmdir(const char *path);
int vfs_readlink(const char *path, char *buf, size_t bufsz);

int vfs_check_perm(struct fs_file *f, int access_mode);

struct mount_point *find_mount(const char *path);
const char *get_relative_path(const char *path, struct mount_point *mp);
#endif
