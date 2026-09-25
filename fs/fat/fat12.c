#include "fat.h"
#include <kernel/blkdev.h>

extern void put_u64(uint64_t v);

int fat12_mount(struct blkdev *dev, void **sbp)
{

    uint8_t b[512];
    if (dev->read(dev, 0, b, 512))
        return -1;
    if (b[510] != 0x55 || b[511] != 0xAA)
        return -1;
    uint32_t spc = b[13];
    uint32_t reserved = b[14] | (b[15] << 8);
    uint32_t nfats = b[16];
    uint32_t fatsz = b[22] | (b[23] << 8);
    if (fatsz == 0)
        fatsz = b[36] | (b[37] << 8) | (b[38] << 16) | ((uint32_t)b[39] << 24);

    uint32_t tot_sec16 = b[19] | (b[20] << 8);
    uint32_t total_sectors =
        tot_sec16 ? tot_sec16 : (b[32] | (b[33] << 8) | (b[34] << 16) | ((uint32_t)b[35] << 24));

    uint32_t datasec = reserved + nfats * fatsz;
    uint32_t data_sectors = total_sectors - datasec;
    uint32_t total_cl = data_sectors / spc;

    if (total_cl >= 4085) {
        return -1;
    }

    return fat_init_sb(dev, sbp, 12);
}

struct fs_ops fat12_fs = {.name = "fat12",
                          .mount = fat12_mount,
                          .open = fat_open,
                          .read = fat_read,
                          .write = fat_write,
                          .create = fat_create,
                          .readdir = fat_readdir,
                          .unlink = fat_unlink,
                          .mkdir = fat_mkdir,
                          .rmdir = fat_rmdir};
