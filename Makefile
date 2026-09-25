CC=gcc
AS=as
LD=ld
CFLAGS=-m64 -Wall -Wextra -nostdlib -ffreestanding -g -mno-red-zone -mcmodel=kernel
CFLAGS+=-Iinclude -Iinclude/generated
ASFLAGS=--64
LDFLAGS=-T linker.ld -g
BUILD_DIR=build

ifneq ($(wildcard .config),)
include .config
endif

SFILES := arch/x86/boot.s \
          arch/x86/isr.s \
          arch/x86/switch.s \
          arch/x86/syscall.s

CFILES := kernel/main.c \
          kernel/gdt.c \
          kernel/idt.c \
          kernel/irq.c \
          kernel/pic.c \
          kernel/pit.c \
          kernel/pmm.c \
          kernel/vm.c \
          kernel/string.c \
          kernel/console.c \
          kernel/panic.c \
          kernel/multiboot.c \
          kernel/kmalloc.c \
          kernel/bcache.c \
          kernel/proc.c \
          kernel/syscall.c \
          kernel/exec.c \
          kernel/elf.c \
          kernel/binfmt.c \
          kernel/blkdev.c \
          kernel/user.c

ifeq ($(CONFIG_SERIAL_UART),y)
    CFILES += drivers/serial.c
endif

ifeq ($(CONFIG_VGA_TEXT),y)
    CFILES += drivers/vga.c
endif

ifeq ($(CONFIG_TTY),y)
	CFILES += kernel/tty.c kernel/pty.c
endif

ifeq ($(CONFIG_PS2_KEYBOARD),y)
    CFILES += drivers/keyboard.c
endif

ifeq ($(CONFIG_ATA),y)
    CFILES += drivers/ata.c
endif

ifeq ($(CONFIG_RAMDISK),y)
    CFILES += drivers/ramdisk.c
endif

ifeq ($(CONFIG_PCI_RAMDISK),y)
    CFILES += drivers/pci_ramdisk.c
endif

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

ifeq ($(CONFIG_NET),y)
    CFILES += net/socket.c net/ipv4.c net/tcp.c
endif

ifeq ($(CONFIG_NET_RTL8139),y)
    CFILES += drivers/net/rtl8139.c
endif

ifeq ($(CONFIG_NET_E1000),y)
	CFILES += drivers/net/e1000.c
endif

OBJS := $(CFILES:%.c=$(BUILD_DIR)/%.o) $(SFILES:%.s=$(BUILD_DIR)/%.o)

.PHONY: all kernel run test menuconfig clean distclean check_config user install-init

all: kernel

$(BUILD_DIR)/%.o: %.c include/generated/autoconf.h
	@mkdir -p $(dir $@)
	@echo "[*] CC: $<"
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.s
	@mkdir -p $(dir $@)
	@echo "[*] AS: $<"
	@$(AS) $(ASFLAGS) $< -o $@

include/generated/autoconf.h: .config
	@echo "[*] Generating autoconf.h"
	@python3 scripts/copper_config.py --generate-headers

kernel: check_config $(OBJS)
	@echo "[*] LD: copper.bin"
	@$(LD) $(LDFLAGS) $(OBJS) -o $(BUILD_DIR)/copper.elf
	@objcopy -O binary $(BUILD_DIR)/copper.elf $(BUILD_DIR)/copper.bin
	@echo "[*] Build complete"

test: kernel
	@echo "[*] ISO boot (GRUB) + real disk"
	@bash scripts/build_iso.sh $(BUILD_DIR) $(BUILD_DIR)/copper.bin $(BUILD_DIR)/copper.iso
	@qemu-system-x86_64 -m 1G -cdrom $(BUILD_DIR)/copper.iso $(if $(wildcard fs.img),-hda fs.img,) -serial stdio -no-reboot -no-shutdown -boot d

run: kernel
	@echo "[*] Direct boot + real disk"
	@qemu-system-x86_64 -m 256M -kernel $(BUILD_DIR)/copper.bin $(if $(wildcard fs.img),-hda fs.img,) -serial stdio -no-reboot -no-shutdown -netdev user,id=net0,net=10.0.2.0/24,hostfwd=udp::8080-:8080,hostfwd=tcp::8080-:8080 -device e1000,netdev=net0,mac=52:54:00:12:34:57 -device ivshmem-plain,memdev=ramdisk0 -object memory-backend-ram,id=ramdisk0,size=64M -boot d
	
check_config:
ifeq ($(wildcard .config),)
	$(error "No .config! Run 'make menuconfig' first")
endif

scripts/menuconfig: scripts/menuconfig.c
	@echo "[*] Building menuconfig..."
	@gcc scripts/menuconfig.c -lncurses -o scripts/menuconfig

menuconfig: scripts/menuconfig
	@./scripts/menuconfig
	@echo "[*] Generating autoconf.h..."
	@python3 scripts/copper_config.py --generate-headers

clean:
	@rm -rf $(BUILD_DIR)

distclean: clean
	@rm -f .config
	@rm -rf include/generated
	@rm scripts/menuconfig

commitready: distclean
	@echo "[*] Your code is ready for commit!"
