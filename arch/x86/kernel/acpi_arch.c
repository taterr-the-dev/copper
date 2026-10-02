#include <kernel/acpi.h>
#include <kernel/arch.h>
#include <kernel/string.h>
#include <kernel/io.h>

extern void vm_map(uint64_t *cr3, uint64_t va, uint64_t pa, uint64_t flags);

#define ACPI_MAP_BASE 0xFFFFFF8000000000ULL

void *arch_acpi_map_phys(uint64_t phys_addr, size_t size) {
    uint64_t cr3 = arch_read_cr3();
    uint64_t va = ACPI_MAP_BASE + phys_addr;
    for (size_t i = 0; i < size; i += 4096) {
        vm_map((uint64_t *)cr3, va + i, (phys_addr + i) & ~0xFFF, 0x03);
    }
    return (void *)va;
}

static struct acpi_rsdp *scan_region(uint64_t start, uint64_t end) {
    for (uint64_t addr = start; addr < end; addr += 16) {
        void *mapped = arch_acpi_map_phys(addr & ~0xFFF, 4096);
        uint64_t offset = addr & 0xFFF;
        char *ptr = (char *)mapped + offset;
        if (ptr[0] == 'R' && ptr[1] == 'S' && ptr[2] == 'D' &&
            ptr[3] == ' ' && ptr[4] == 'P' && ptr[5] == 'T' &&
            ptr[6] == 'R' && ptr[7] == ' ') {
            return (struct acpi_rsdp *)ptr;
        }
    }
    return NULL;
}

struct acpi_rsdp *arch_acpi_get_rsdp(void) {
    struct acpi_rsdp *rsdp = NULL;

    rsdp = scan_region(0x9FC00, 0x9FFFF);
    if (rsdp) return rsdp;

    rsdp = scan_region(0xE0000, 0xFFFFF);
    if (rsdp) return rsdp;

    return NULL;
}

void arch_acpi_pm_write_gas(struct acpi_generic_address *gas, uint16_t val) {
    if (gas->address == 0) return;

    if (gas->address_space_id == 1) {
        outw((uint32_t)gas->address, val);
    } else if (gas->address_space_id == 0) {
        void *mapped = arch_acpi_map_phys(gas->address & ~0xFFF, 4096);
        volatile uint16_t *reg = (volatile uint16_t *)((uint8_t *)mapped + (gas->address & 0xFFF));
        *reg = val;
    }
}

void arch_acpi_shutdown_qemu(uint16_t val) {
    outw(0x604, val);
    outw(0xB004, val);
    outl(0xf4, 0x31);
}
