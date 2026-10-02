#include "ext.h"
#include <kernel/blkdev.h>

int ext2_mount(struct blkdev *dev, void **sbp)
{
    uint8_t sb[1024];
    if (dev->read(dev, 1024, sb, 1024)) {
        return -1;
    }
    if ((sb[56] | (sb[57] << 8)) != 0xEF53) {
        return -1;
    }
    uint32_t fc = sb[0x5C] | (sb[0x5D] << 8) | (sb[0x5E] << 16) | ((uint32_t)sb[0x5F] << 24);
    if (fc & 0x0004) {
        return -1;
    }
    return ext_init_sb(dev, sbp);
}

struct fs_ops ext2_fs = {
  .name = "ext2",
  .mount = ext2_mount,
  .open = ext_open,
  .close = ext_close,
  .read = ext_read,
  .write = ext_write,
  .create = ext_create,
  .readdir = ext_readdir,
  .unlink = ext_unlink,
  .mkdir = ext_mkdir,
  .rmdir = ext_rmdir,
  .readlink = ext_readlink,
  .truncate = ext_truncate,
  .rename = ext_rename
};
