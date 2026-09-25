#ifndef _KERNEL_BLKDEV_H
#define _KERNEL_BLKDEV_H
#include <kernel/types.h>
struct blkdev {
    const char *name;
    uint64_t size;
    uint32_t block_size;
    int (*read)(struct blkdev *, uint64_t, void *, size_t);
    int (*write)(struct blkdev *, uint64_t, const void *, size_t);
    void *priv;
};
int blkdev_register(struct blkdev *);
struct blkdev *blkdev_by_name(const char *);
int ramdisk_attach(uint64_t, uint64_t);
#endif
int ata_init(void);
int blkdev_count(void);
struct blkdev *blkdev_get(int i);
void blkdev_scan_partitions(struct blkdev *);
