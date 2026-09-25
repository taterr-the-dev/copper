#!/ bin / bash
set - e IMG = image.img MNT = / tmp / copper_disk FSRC =
                                  / tmp / copper_fssrc SIZE = 64

    dd if =
        / dev / zero of = $IMG bs = 1M count =
            $SIZE
                printf 'label: dos\nunit: sectors\n1 : start=2048, type=83, bootable\n' |
            sfdisk - q $IMG

                         LOOP =
                $(sudo losetup - f) sudo losetup - P $LOOP $IMG sudo mkfs.ext3 -
                q ${LOOP} p1 sudo mkdir -
                p $MNT sudo mount ${LOOP} p1 $MNT

                    FLOOP =
                    $(sudo losetup - f) sudo losetup $FLOOP fs.img sudo mkdir -
                        p $FSRC sudo mount - o ro $FLOOP $FSRC sudo cp -
                        a $FSRC /.$MNT / sudo umount $FSRC sudo losetup -
                        d $FLOOP

                            sudo mkdir -
                        p $MNT / boot / grub sudo cp build / copper.bin $MNT /
                            boot / sudo tee $MNT / boot / grub / grub.cfg >
                    / dev / null << 'CFG' set default = 0 set timeout =
                        2 menuentry
                        "Copper" {multiboot / boot / copper.bin boot} CFG echo
                        "(hd0)   $LOOP" |
                        sudo tee $MNT / boot / grub / device.map >
                            / dev / null sudo grub2 - install-- target =
                            i386 - pc-- boot - directory =
                                $MNT / boot-- root - directory =
                                    $MNT-- no -
                                    floppy $LOOP

                                        sudo umount $MNT sudo losetup -
                                    d $LOOP echo "[*] $IMG ready."
