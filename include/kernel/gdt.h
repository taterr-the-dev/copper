#ifndef _KERNEL_GDT_H
#define _KERNEL_GDT_H

#include <kernel/types.h>

void gdt_init(void);

#endif
void gdt_set_kernel_stack(uint64_t rsp0);
