#ifndef _KERNEL_ARCH_H
#define _KERNEL_ARCH_H

#include <kernel/types.h>

void arch_cli(void);
void arch_sti(void);
void arch_hlt(void);

uint64_t arch_save_flags(void);
void arch_restore_flags(uint64_t flags);

void arch_switch_cr3(uint64_t cr3);
uint64_t arch_read_cr3(void);
uint64_t arch_read_cr2(void);
void arch_invlpg(uint64_t addr);

void arch_wrmsr(uint32_t msr, uint64_t val);
uint64_t arch_rdmsr(uint32_t msr);

void arch_init_fpu(void);

#endif
