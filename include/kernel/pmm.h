#ifndef _KERNEL_PMM_H
#define _KERNEL_PMM_H

#include <kernel/types.h>

void pmm_init(void);
uint64_t pmm_alloc(void);
void pmm_free(uint64_t addr);

void pmm_add_region(uint64_t base, uint64_t len);
void pmm_reserve_region(uint64_t base, uint64_t len);

void pmm_reserve(uint64_t lo, uint64_t hi);

#endif
