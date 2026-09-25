#ifndef _KERNEL_PMM_H
#define _KERNEL_PMM_H
#include <kernel/types.h>
void pmm_init(void);
uint64_t pmm_alloc(void);
void pmm_free(uint64_t addr);
#endif
void pmm_reserve(uint64_t, uint64_t);
