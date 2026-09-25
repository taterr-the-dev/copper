#include <autoconf.h>
#include <kernel/console.h>
#include <kernel/io.h>
#include <kernel/pmm.h>
#include <kernel/vm.h>
#include <kernel/types.h>
#include <kernel/string.h>

extern void put_u64(uint64_t v);
extern void hex64(uint64_t v);

#ifdef CONFIG_PCI_RAMDISK

extern uint64_t boot_cr3;

struct pci_ramdisk {
    void *mmio_base;
    uint64_t mmio_phys;
    uint64_t size;
    int active;
};

static struct pci_ramdisk ramdisk = {0};

static uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = (uint32_t)((1 << 31) | (bus << 16) | (slot << 11) |
                                  (func << 8) | (offset & 0xFC));
    outl(0xCF8, address);
    return inl(0xCFC);
}

int pci_ramdisk_init(void) {
    for (int bus = 0; bus < 256; bus++) {
        for (int slot = 0; slot < 32; slot++) {
            uint32_t id = pci_config_read32(bus, slot, 0, 0);
            uint16_t vendor = id & 0xFFFF;
            uint16_t device = (id >> 16) & 0xFFFF;
            if (vendor == 0x1af4 && device == 0x1110) {
                con_puts("[PCI-RAMDISK] Found ivshmem device at ");
                put_u64(bus); con_puts(":"); put_u64(slot); con_puts("\n");
                
                uint32_t bar2 = pci_config_read32(bus, slot, 0, 0x18);
                ramdisk.mmio_phys = bar2 & ~0xF;
                
                uint64_t cr3;
                __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
                
                for (int i = 0; i < 16384; i++) {
                    vm_map((uint64_t *)cr3, 
                           0xFFFF800000000000ULL + i * 4096,
                           ramdisk.mmio_phys + i * 4096, 
                           0x03);
                }
                
                ramdisk.mmio_base = (void *)0xFFFF800000000000ULL;
                ramdisk.size = 64 * 1024 * 1024;
                ramdisk.active = 1;
                
                con_puts("[PCI-RAMDISK] Mapped 64MB at ");
                hex64((uint64_t)ramdisk.mmio_base);
                con_puts("\n");
                
                return 0;
            }
        }
    }
    
    con_puts("[PCI-RAMDISK] No ivshmem device found\n");
    return -1;
}

int pci_ramdisk_read(uint64_t offset, void *buf, uint64_t size) {
    if (!ramdisk.active) return -1;
    if (offset + size > ramdisk.size) return -1;
    
    memcpy(buf, (uint8_t *)ramdisk.mmio_base + offset, size);
    return 0;
}

int pci_ramdisk_write(uint64_t offset, const void *buf, uint64_t size) {
    if (!ramdisk.active) return -1;
    if (offset + size > ramdisk.size) return -1;
    
    memcpy((uint8_t *)ramdisk.mmio_base + offset, buf, size);
    return 0;
}

int pci_ramdisk_is_active(void) {
    return ramdisk.active;
}

#endif
