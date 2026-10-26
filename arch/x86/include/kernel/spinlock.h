#ifndef _KERNEL_SPINLOCK_H
#define _KERNEL_SPINLOCK_H

#include <kernel/types.h>

#define SPINLOCK_DEBUG 1
#define SPIN_TIMEOUT   (1ULL << 26)

typedef struct {
    volatile uint32_t locked;
#if SPINLOCK_DEBUG
    void *owner_pc;
#endif
} spinlock_t;

#define SPINLOCK_INIT { 0 }

#define DEFINE_SPINLOCK(n) \
    static spinlock_t n __attribute__((aligned(64))) = SPINLOCK_INIT

void spin_bug(const char *msg, spinlock_t *l, void *pc);

static inline void spinlock_init(spinlock_t *l) { l->locked = 0; }

static inline uint64_t irq_save(void) {
    uint64_t f;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(f) :: "memory");
    return f;
}
static inline void irq_restore(uint64_t f) {
    if (f & 0x200) __asm__ volatile("sti" ::: "memory");
}

static inline void spin_lock(spinlock_t *l) {
    while (__atomic_exchange_n(&l->locked, 1, __ATOMIC_ACQUIRE)) {
#if SPINLOCK_DEBUG
        uint64_t n = 0;
#endif

        while (__atomic_load_n(&l->locked, __ATOMIC_RELAXED)) {
            __builtin_ia32_pause();
#if SPINLOCK_DEBUG
            if (++n > SPIN_TIMEOUT)
                spin_bug("stuck", l, __builtin_return_address(0));
#endif
        }
    }
#if SPINLOCK_DEBUG
    l->owner_pc = __builtin_return_address(0);
#endif
}

static inline int spin_trylock(spinlock_t *l) {
    if (__atomic_load_n(&l->locked, __ATOMIC_RELAXED)) return 0;
    if (__atomic_exchange_n(&l->locked, 1, __ATOMIC_ACQUIRE)) return 0;
#if SPINLOCK_DEBUG
    l->owner_pc = __builtin_return_address(0);
#endif
    return 1;
}

static inline void spin_unlock(spinlock_t *l) {
#if SPINLOCK_DEBUG
    if (!__atomic_load_n(&l->locked, __ATOMIC_RELAXED))
        spin_bug("unlock of free lock", l, __builtin_return_address(0));
    l->owner_pc = 0;
#endif
    __atomic_store_n(&l->locked, 0, __ATOMIC_RELEASE);
}

static inline void spin_lock_irqsave(spinlock_t *l, uint64_t *flags) {
    *flags = irq_save();
    spin_lock(l);
}
static inline void spin_unlock_irqrestore(spinlock_t *l, uint64_t flags) {
    spin_unlock(l);
    irq_restore(flags);
}

#endif
