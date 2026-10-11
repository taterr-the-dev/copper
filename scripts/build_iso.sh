#!/bin/bash
set -e

BUILD_DIR=$1
KERNEL_BIN=$2
ISO_OUT=$3

if [ -z "$BUILD_DIR" ] || [ -z "$KERNEL_BIN" ] || [ -z "$ISO_OUT" ]; then
    echo "Usage: $0 <build_dir> <kernel_bin> <iso_out>"
    exit 1
fi

if command -v grub2-mkimage > /dev/null 2>&1; then
    GRUB_MKIMAGE=grub2-mkimage
else
    GRUB_MKIMAGE=grub-mkimage
fi

GRUB_MOD_DIR=""
for d in /usr/lib/grub/i386-pc /usr/lib64/grub/i386-pc /usr/share/grub/i386-pc; do
    if [ -f "$d/cdboot.img" ]; then
        GRUB_MOD_DIR="$d"
        break
    fi
done

if [ -z "$GRUB_MOD_DIR" ]; then
    echo "[!] Could not find GRUB i386-pc modules."
    echo "[!] Fedora: sudo dnf install grub2-pc-modules xorriso"
    exit 1
fi

echo "[*] Using GRUB modules from: $GRUB_MOD_DIR"

ISO_DIR="$BUILD_DIR/isodir"
rm -rf "$ISO_DIR"
mkdir -p "$ISO_DIR/boot/grub/i386-pc"

cp "$KERNEL_BIN" "$ISO_DIR/boot/copper.bin"

cat > "$ISO_DIR/boot/grub/grub.cfg" << CFG
set timeout=0
set default=0
set gfxmode=1024x768x32
set gfxpayload=keep
menuentry "Copper Kernel" {
    echo "Loading Copper kernel..."
    multiboot --quirk-modules-after-kernel /boot/copper.bin
    echo "Booting Copper kernel..."
    boot
}
CFG

echo "[*] Generating GRUB core.img..."
"$GRUB_MKIMAGE" -d "$GRUB_MOD_DIR" -O i386-pc -o "$ISO_DIR/boot/grub/i386-pc/core.img" -p "(cd)/boot/grub" multiboot iso9660 biosdisk boot normal configfile echo

echo "[*] Creating El Torito boot image..."
cat "$GRUB_MOD_DIR/cdboot.img" "$ISO_DIR/boot/grub/i386-pc/core.img" > "$ISO_DIR/boot/grub/i386-pc/eltorito.img"

echo "[*] Building ISO with xorriso..."
xorriso -as mkisofs -b boot/grub/i386-pc/eltorito.img -no-emul-boot -boot-load-size 4 -boot-info-table -o "$ISO_OUT" "$ISO_DIR"

echo "[*] ISO created successfully: $ISO_OUT"
