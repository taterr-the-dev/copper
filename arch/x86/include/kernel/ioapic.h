#ifndef _KERNEL_IOAPIC_H
#define _KERNEL_IOAPIC_H
#include <kernel/types.h>

void ioapic_init(uint64_t phys_addr);
void ioapic_route_irq(uint8_t irq, uint8_t vector, uint8_t dest_apic_id);

#endif
