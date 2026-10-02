#include <kernel/arch.h>

void arch_cli(void) {
    __asm__ volatile("cli");
}

void arch_sti(void) {
    __asm__ volatile("sti");
}

void arch_hlt(void) {
    __asm__ volatile("hlt");
}

uint64_t arch_save_flags(void) {
    uint64_t flags;
    __asm__ volatile("pushfq; pop %0" : "=r"(flags));
    return flags;
}

void arch_restore_flags(uint64_t flags) {
    __asm__ volatile("push %0; popfq" ::"r"(flags) : "memory", "cc");
}

void arch_switch_cr3(uint64_t cr3) {
    __asm__ volatile("mov %0, %%cr3" ::"r"(cr3) : "memory");
}

uint64_t arch_read_cr3(void) {
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

uint64_t arch_read_cr2(void) {
    uint64_t cr2;
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
    return cr2;
}

void arch_invlpg(uint64_t addr) {
    __asm__ volatile("invlpg (%0)" ::"r"(addr) : "memory");
}

void arch_wrmsr(uint32_t msr, uint64_t val) {
    __asm__ volatile("wrmsr" :: "c"(msr), "a"((uint32_t)val), "d"((uint32_t)(val >> 32)));
}

uint64_t arch_rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

void arch_init_fpu(void) {
    uint64_t cr0, cr4;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~0x04; cr0 |= 0x22; 
    __asm__ volatile("mov %0, %%cr0" ::"r"(cr0));

    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= 0x0600; 
    __asm__ volatile("mov %0, %%cr4" ::"r"(cr4));

    __asm__ volatile("fninit");
    uint32_t mxcsr = 0x1F80;
    __asm__ volatile("ldmxcsr %0" ::"m"(mxcsr));
}
