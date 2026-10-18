#ifndef _KERNEL_GDT_H
#define _KERNEL_GDT_H

#include <kernel/types.h>

void gdt_init(void);
void gdt_init_cpu(uint32_t cpu_id);
void gdt_set_kernel_stack(uint64_t rsp0);
uint64_t gdt_get_ap_stack(uint32_t cpu_id);

#endif
