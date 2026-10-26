#include <kernel/spinlock.h>
#include <kernel/io.h>

static void raw_puts(const char *s) {
    for (; *s; s++) {
        while (!(inb(0x3FD) & 0x20)) { }
        outb(0x3F8, *s);
    }
}
static void raw_hex(uint64_t v) {
    static const char d[] = "0123456789abcdef";
    raw_puts("0x");
    for (int i = 60; i >= 0; i -= 4) {
        char s[2] = { d[(v >> i) & 0xf], 0 };
        raw_puts(s);
    }
}

void spin_bug(const char *msg, spinlock_t *l, void *pc) {
    __asm__ volatile("cli");
    raw_puts("\n[LOCK] "); raw_puts(msg);
    raw_puts(" lock=");  raw_hex((uint64_t)l);
#if SPINLOCK_DEBUG
    raw_puts(" owner="); raw_hex((uint64_t)l->owner_pc);
#endif
    raw_puts(" caller="); raw_hex((uint64_t)pc);
    raw_puts("\n");
    for (;;) __asm__ volatile("hlt");
}
