#include "ext.h"
#include <kernel/blkdev.h>

int ext4_mount(struct blkdev *dev, void **sbp)
{
    uint8_t sb[1024];
    if (dev->read(dev, 1024, sb, 1024)) {
        return -1;
    }
    if ((sb[56] | (sb[57] << 8)) != 0xEF53) {
        return -1;
    }
    uint32_t fic = ext_rd32(sb + 0x60);
    uint32_t fro = ext_rd32(sb + 0x64);

    uint32_t ext4_incompat = EXT_INCOMPAT_EXTENTS | EXT_INCOMPAT_64BIT | EXT_INCOMPAT_FLEX_BG;
    uint32_t ext4_ro = EXT_RO_HUGE_FILE | EXT_RO_GDT_CSUM | EXT_RO_DIR_NLINK | EXT_RO_EXTRA_ISIZE |
                       EXT_RO_METADATA_CSUM;
    if (!(fic & ext4_incompat) && !(fro & ext4_ro)) {
        return -1;
    }
    return ext_init_sb(dev, sbp);
}

struct fs_ops ext4_fs = {
	.name = "ext4",
	.mount = ext4_mount,
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
