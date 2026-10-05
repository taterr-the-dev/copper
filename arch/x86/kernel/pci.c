#include <kernel/pci.h>
#include <kernel/io.h>
#include <kernel/console.h>
#include <kernel/string.h>

extern void put_u64(uint64_t v);

struct pci_device pci_devices[PCI_MAX_DEVICES];
int pci_device_count = 0;

uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = (uint32_t)((1 << 31) | (bus << 16) | (slot << 11) |
                                  (func << 8) | (offset & 0xFC));
    outl(0xCF8, address);
    return inl(0xCFC);
}

uint16_t pci_config_read16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t data = pci_config_read32(bus, slot, func, offset & 0xFC);
    return (uint16_t)((data >> ((offset & 2) * 8)) & 0xFFFF);
}

void pci_config_write16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t val) {
    uint32_t address = (uint32_t)((1 << 31) | (bus << 16) | (slot << 11) |
                                  (func << 8) | (offset & 0xFC));
    outl(0xCF8, address);
    uint32_t tmp = inl(0xCFC);
    if (offset & 2) {
        tmp = (tmp & 0x0000FFFF) | ((uint32_t)val << 16);
    } else {
        tmp = (tmp & 0xFFFF0000) | val;
    }
    outl(0xCFC, tmp);
}

struct pci_device *pci_find_device(uint16_t vendor, uint16_t device) {
    for (int i = 0; i < pci_device_count; i++) {
        if (pci_devices[i].vendor_id == vendor && pci_devices[i].device_id == device) {
            return &pci_devices[i];
        }
    }
    return NULL;
}

void pci_scan_bus(uint8_t bus) {
    for (uint8_t slot = 0; slot < 32; slot++) {
        uint32_t id = pci_config_read32(bus, slot, 0, 0);
        if (id == 0xFFFFFFFF) continue;

        uint16_t vendor = id & 0xFFFF;
        uint16_t device = (id >> 16) & 0xFFFF;
        if (vendor == 0xFFFF) continue;
        if (pci_device_count >= PCI_MAX_DEVICES) return;

        struct pci_device *dev = &pci_devices[pci_device_count];
        memset(dev, 0, sizeof(struct pci_device));
        dev->bus = bus;
        dev->slot = slot;
        dev->func = 0;
        dev->vendor_id = vendor;
        dev->device_id = device;

        uint32_t class_info = pci_config_read32(bus, slot, 0, 0x08);
        dev->class_code = (class_info >> 24) & 0xFF;
        dev->subclass = (class_info >> 16) & 0xFF;
        dev->prog_if = (class_info >> 8) & 0xFF;

        dev->bar0 = pci_config_read32(bus, slot, 0, 0x10);
        dev->bar1 = pci_config_read32(bus, slot, 0, 0x14);
        dev->bar2 = pci_config_read32(bus, slot, 0, 0x18);
        dev->bar3 = pci_config_read32(bus, slot, 0, 0x1C);
        dev->bar4 = pci_config_read32(bus, slot, 0, 0x20);
        dev->bar5 = pci_config_read32(bus, slot, 0, 0x24);

        uint16_t irq_info = pci_config_read16(bus, slot, 0, 0x3C);
        dev->irq_line = irq_info & 0xFF;
        dev->irq_pin = (irq_info >> 8) & 0xFF;

        pci_device_count++;
    }
}

void pci_init(void) {
    con_puts("[PCI] Scanning bus 0...\n");
    pci_scan_bus(0);
    con_puts("[PCI] Found ");
    put_u64(pci_device_count);
    con_puts(" device(s)\n");
}
