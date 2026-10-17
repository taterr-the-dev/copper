#ifndef _KERNEL_PERCPU_H
#define _KERNEL_PERCPU_H

#include <kernel/types.h>

struct task;

#ifndef offsetof
#define offsetof(TYPE, MEMBER) __builtin_offsetof(TYPE, MEMBER)
#endif

struct percpu {
    uint32_t apic_id;
    uint32_t cpu_id;
    uint64_t kernel_stack;
    struct task *current;
    void *gdt_ptr;
    void *tss_ptr;
};

#define percpu_read(field) \
    ({ \
        typeof(((struct percpu *)0)->field) __val; \
        __asm__ volatile("mov %%gs:%c1, %0" : "=r"(__val) : "i"(offsetof(struct percpu, field))); \
        __val; \
    })

#define percpu_write(field, val) \
    do { \
        typeof(((struct percpu *)0)->field) __val = (val); \
        __asm__ volatile("mov %0, %%gs:%c1" :: "r"(__val), "i"(offsetof(struct percpu, field))); \
    } while (0)

void percpu_init_bsp(void);

#endif
