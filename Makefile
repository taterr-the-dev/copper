UNAME_M := $(shell uname -m)

ifeq ($(UNAME_M),x86_64)
    DETECTED_ARCH := x86
else ifeq ($(UNAME_M),i686)
    DETECTED_ARCH := x86
else ifeq ($(UNAME_M),aarch64)
    DETECTED_ARCH := arm64
else ifeq ($(UNAME_M),arm64)
    DETECTED_ARCH := arm64
else ifeq ($(findstring riscv,$(UNAME_M)),riscv)
    DETECTED_ARCH := riscv
else
    DETECTED_ARCH := $(UNAME_M)
endif

ARCH ?= $(DETECTED_ARCH)

CC=gcc
AS=as
LD=ld

CFLAGS=-m64 -Wall -Wextra -nostdlib -ffreestanding -g -mno-red-zone -mcmodel=kernel
CFLAGS+=-Iinclude -Iinclude/generated -Iarch/$(ARCH)/include
ASFLAGS=--64
LDFLAGS=-T linker.ld -g
BUILD_DIR=build

ifneq ($(wildcard .config),)
include .config
endif

CFILES :=
SFILES :=

-include arch/$(ARCH)/Makefile.inc

include kernel/Makefile.inc
include fs/Makefile.inc
include drivers/Makefile.inc
include net/Makefile.inc

OBJS := $(CFILES:%.c=$(BUILD_DIR)/%.o) $(SFILES:%.s=$(BUILD_DIR)/%.o)

.PHONY: all kernel run test menuconfig clean distclean check_config user

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
	@qemu-system-x86_64 -m 256M -kernel $(BUILD_DIR)/copper.bin $(if $(wildcard fs.img),-hda fs.img,) -serial stdio -no-reboot -no-shutdown -netdev user,id=net0,net=10.0.2.0/24,hostfwd=udp::8080-:8080,hostfwd=tcp::8090-:8090 -device e1000,netdev=net0,mac=52:54:00:12:34:57 -device ivshmem-plain,memdev=ramdisk0 -object memory-backend-ram,id=ramdisk0,size=64M -boot d

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
	@rm -f scripts/menuconfig
