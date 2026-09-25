#ifndef _KERNEL_VM_H
#define _KERNEL_VM_H
#include <kernel/types.h>
#define MMAP_BASE 0x40000000ULL
extern uint64_t boot_cr3;
void vm_init(void);
uint64_t *vm_new_as(void);
void vm_map(uint64_t *pml4, uint64_t va, uint64_t pa, uint64_t flags);
uint64_t *vm_clone_as(uint64_t *src);
int vm_cow_handle(uint64_t *cr3, uint64_t va);
#endif
void vm_sanitize_boot_cr3(void);
