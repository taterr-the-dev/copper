#!/ bin / bash
set - e

          ORIG_IMG = "fs.img" BACKUP_DIR = "/tmp/fs_backup_$$" MOUNT_DIR =
    "/tmp/fs_mount_$$"

    GREEN = '\033[0;32m' YELLOW = '\033[1;33m' RED = '\033[0;31m' NC = '\033[0m'

    if[!-f "$ORIG_IMG"];
then echo - e "${RED}Error: $ORIG_IMG not found!${NC}" exit 1 fi

                ORIG_SIZE =
    $(stat - c % s "$ORIG_IMG") echo -
            e "${YELLOW}Original image size: $ORIG_SIZE bytes ($(( ORIG_SIZE / "
              "1024 / 1024 )) MB)${NC}"

            mkdir -
            p "$BACKUP_DIR"
              "$MOUNT_DIR"

            echo -
            e "${YELLOW}Backing up original contents...${NC}" sudo mount -
            o loop "$ORIG_IMG"
                   "$MOUNT_DIR" sudo cp -
            a "$MOUNT_DIR/."
              "$BACKUP_DIR/" 2 >
        / dev / null ||
    true sudo umount "$MOUNT_DIR"

        create_fs(){
            local name = $1 local mkfs_cmd = $2 local size = $3 local img_name =
                "fs_${name}.img"

                        echo -
                        e "${YELLOW}Creating $img_name (${size} bytes / $(( "
                          "size / "
                          "1024 / 1024 )) MB)...${NC}"

                        truncate -
                        s "$size"
                          "$img_name" sudo $mkfs_cmd -
                        F "$img_name" >
                    / dev / null 2 > &1 ||
                {echo - e "${RED}✗ Failed to format $img_name${NC}" return 1}

                        sudo mount -
                        o loop "$img_name"
                               "$MOUNT_DIR" sudo cp -
                        a "$BACKUP_DIR/."
                          "$MOUNT_DIR/" 2 >
                    / dev / null ||
                true sudo umount "$MOUNT_DIR"

                    echo -
                    e "${GREEN}✓ Created $img_name${NC}"}

        create_fs "ext2"
                  "mkfs.ext2"
                  "$ORIG_SIZE" create_fs "ext3"
                  "mkfs.ext3"
                  "$ORIG_SIZE" create_fs "ext4"
                  "mkfs.ext4"
                  "$ORIG_SIZE"

        create_fs "fat12"
                  "mkfs.fat -F 12"
                  "1474560"

        if["$ORIG_SIZE" - ge 16777216] &&
        ["$ORIG_SIZE" - le 2147483648];
then create_fs "fat16"
               "mkfs.fat -F 16"
               "$ORIG_SIZE" else echo -
    e "${YELLOW}Adjusting FAT16 to 32 MB (original size "
      "incompatible)${NC}" create_fs "fat16"
      "mkfs.fat -F 16"
      "33554432" fi

    if["$ORIG_SIZE" - ge 536870912];
then create_fs "fat32"
               "mkfs.fat -F 32"
               "$ORIG_SIZE" else echo -
    e "${YELLOW}Adjusting FAT32 to 512 MB (original size too "
      "small)${NC}" create_fs "fat32"
      "mkfs.fat -F 32"
      "536870912" fi

          rm -
    rf "$BACKUP_DIR"
       "$MOUNT_DIR"

    echo -
    e "${GREEN}========================================${NC}" echo -
    e "${GREEN}All filesystems created!${NC}" echo -
    e "${GREEN}========================================${NC}" ls - lh fs_ *.img
