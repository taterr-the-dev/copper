#ifndef _KERNEL_MULTIBOOT_H
#define _KERNEL_MULTIBOOT_H
#include <kernel/types.h>
void mb_init(uint64_t mbi);
int mb_module_count(void);
int mb_module(int i, uint64_t *start, uint64_t *end);
#endif
