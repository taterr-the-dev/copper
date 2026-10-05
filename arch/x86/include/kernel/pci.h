#ifndef _KERNEL_PCI_H
#define _KERNEL_PCI_H

#include <kernel/types.h>

#define PCI_MAX_DEVICES 32

struct pci_device {
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint32_t bar0;
    uint32_t bar1;
    uint32_t bar2;
    uint32_t bar3;
    uint32_t bar4;
    uint32_t bar5;
    uint8_t irq_line;
    uint8_t irq_pin;
};

void pci_init(void);
uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
uint16_t pci_config_read16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void pci_config_write16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t val);

struct pci_device *pci_find_device(uint16_t vendor, uint16_t device);

extern struct pci_device pci_devices[PCI_MAX_DEVICES];
extern int pci_device_count;

#endif
