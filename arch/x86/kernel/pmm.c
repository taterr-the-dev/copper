#include <autoconf.h>
#ifdef CONFIG_VM
#include <kernel/console.h>
#include <kernel/pmm.h>
#include <kernel/string.h>
#include <kernel/spinlock.h>

#define BASE 0x100000ULL
#define MAX_MEM (1024ULL * 1024 * 1024)
#define NF (MAX_MEM / 4096)
static uint8_t bmp[NF / 8];
static spinlock_t pmm_lock = SPINLOCK_INIT;

void pmm_init(void) {
    memset(bmp, 0xFF, sizeof(bmp));
    spinlock_init(&pmm_lock);
    con_puts("[PMM] Initialized.\n");
}

void pmm_add_region(uint64_t base, uint64_t len) {
    uint64_t flags;
    spin_lock_irqsave(&pmm_lock, &flags);
    for (uint64_t a = base; a < base + len; a += 4096) {
        if (a < BASE) continue;
        size_t i = (a - BASE) / 4096;
        if (i < NF) bmp[i / 8] &= ~(1 << (i & 7));
    }
    spin_unlock_irqrestore(&pmm_lock, flags);
}

void pmm_reserve_region(uint64_t base, uint64_t len) {
    uint64_t flags;
    spin_lock_irqsave(&pmm_lock, &flags);
    for (uint64_t a = base; a < base + len; a += 4096) {
        if (a < BASE) continue;
        size_t i = (a - BASE) / 4096;
        if (i < NF) bmp[i / 8] |= 1 << (i & 7);
    }
    spin_unlock_irqrestore(&pmm_lock, flags);
}

void pmm_reserve(uint64_t lo, uint64_t hi) {
    pmm_reserve_region(lo, hi - lo);
}

uint64_t pmm_alloc(void) {
    uint64_t flags;
    uint64_t a = 0;
    spin_lock_irqsave(&pmm_lock, &flags);

    for (size_t i = 0; i < NF; i++) {
        if (!(bmp[i / 8] & (1 << (i & 7)))) {
            bmp[i / 8] |= 1 << (i & 7);
            a = BASE + i * 4096;
            break;
        }
    }
    spin_unlock_irqrestore(&pmm_lock, flags);
    if (a) {
        memset((void *)a, 0, 4096);
        return a;
    }
    con_puts("[PMM] FATAL: Out of physical memory!\n");
    for (;;) __asm__ volatile("cli; hlt");
    return 0;
}

void pmm_free(uint64_t a) {
    if (a < BASE) return;
    size_t i = (a - BASE) / 4096;
    uint64_t flags;
    spin_lock_irqsave(&pmm_lock, &flags);
    if (i < NF) bmp[i / 8] &= ~(1 << (i & 7));
    spin_unlock_irqrestore(&pmm_lock, flags);
}
#endif
