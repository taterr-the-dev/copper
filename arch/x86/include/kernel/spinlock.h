#ifndef _KERNEL_SPINLOCK_H
#define _KERNEL_SPINLOCK_H

#include <kernel/types.h>
#include <kernel/arch.h>

typedef struct {
    volatile uint32_t locked;
} spinlock_t;

#define SPINLOCK_INIT { 0 }

static inline void spinlock_init(spinlock_t *lock) {
    lock->locked = 0;
}

static inline void spin_lock(spinlock_t *lock) {
    uint32_t old;
    do {
        __asm__ volatile (
            "xchg %0, %1"
            : "=r"(old), "+m"(lock->locked)
            : "0"(1)
            : "memory"
        );
    } while (old != 0);
}

static inline void spin_unlock(spinlock_t *lock) {
    __asm__ volatile ("" ::: "memory");
    lock->locked = 0;
}

static inline void spin_lock_irqsave(spinlock_t *lock, uint64_t *flags) {
    *flags = arch_save_flags();
    arch_cli();
    spin_lock(lock);
}

static inline void spin_unlock_irqrestore(spinlock_t *lock, uint64_t flags) {
    spin_unlock(lock);
    arch_restore_flags(flags);
}

#endif
