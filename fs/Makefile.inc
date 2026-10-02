ifeq ($(CONFIG_FS_VFS),y)
    CFILES += fs/vfs.c fs/devfs.c
endif

ifneq ($(CONFIG_FS_FAT12)$(CONFIG_FS_FAT16)$(CONFIG_FS_FAT32),)
    CFILES += fs/fat/sb.c fs/fat/file.c fs/fat/dir.c
endif
ifeq ($(CONFIG_FS_FAT12),y)
    CFILES += fs/fat/fat12.c
endif
ifeq ($(CONFIG_FS_FAT16),y)
    CFILES += fs/fat/fat16.c
endif
ifeq ($(CONFIG_FS_FAT32),y)
    CFILES += fs/fat/fat32.c
endif

ifneq ($(CONFIG_FS_EXT2)$(CONFIG_FS_EXT3)$(CONFIG_FS_EXT4),)
    CFILES += fs/ext/sb.c fs/ext/inode.c fs/ext/dir.c fs/ext/file.c
endif
ifeq ($(CONFIG_FS_EXT2),y)
    CFILES += fs/ext/ext2.c
endif
ifeq ($(CONFIG_FS_EXT3),y)
    CFILES += fs/ext/ext3.c
endif
ifeq ($(CONFIG_FS_EXT4),y)
    CFILES += fs/ext/ext4.c fs/ext/extents.c
endif

ifeq ($(CONFIG_FS_PROC),y)
    CFILES += fs/proc/procfs.c
endif
ifeq ($(CONFIG_FS_TMPFS),y)
    CFILES += fs/tmpfs.c
endif
