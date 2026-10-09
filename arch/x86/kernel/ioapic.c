#include <kernel/ioapic.h>
#include <kernel/console.h>
#include <kernel/string.h>
#include <kernel/arch.h>
#include <kernel/vm.h>

extern void put_u64(uint64_t v);
extern void hex64(uint64_t v);
extern uint64_t boot_cr3;

#define IOAPIC_REGSEL 0x00
#define IOAPIC_WIN    0x10

static volatile uint32_t *ioapic_base = NULL;

static uint32_t ioapic_read(uint32_t reg) {
    ioapic_base[0] = reg;
    return ioapic_base[4];
}

static void ioapic_write(uint32_t reg, uint32_t val) {
    ioapic_base[0] = reg;
    ioapic_base[4] = val;
}

void ioapic_init(uint64_t phys_addr) {
    uint64_t va = 0xFFFFFF8000200000ULL;
    vm_map((uint64_t *)boot_cr3, va, phys_addr & ~0xFFF, 0x03);
    ioapic_base = (volatile uint32_t *)(va + (phys_addr & 0xFFF));

    uint32_t ver = ioapic_read(0x01);
    uint8_t max_entries = ((ver >> 16) & 0xFF) + 1;

    con_puts("[IOAPIC] Initialized at phys=");
    hex64(phys_addr);
    con_puts(" (Max entries: ");
    put_u64(max_entries);
    con_puts(")\n");
}

void ioapic_route_irq(uint8_t irq, uint8_t vector, uint8_t dest_apic_id) {
    if (!ioapic_base) return;
    uint32_t reg_low = 0x10 + (2 * irq);
    uint32_t reg_high = 0x10 + (2 * irq) + 1;

    uint32_t low = vector;
    uint32_t high = ((uint32_t)dest_apic_id) << 24;

    ioapic_write(reg_low, low);
    ioapic_write(reg_high, high);
}
