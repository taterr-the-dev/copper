#include <kernel/smp.h>
#include <kernel/acpi.h>
#include <kernel/console.h>
#include <kernel/string.h>
#include <kernel/io.h>
#include <kernel/gdt.h>

extern uint8_t trampoline_start;
extern uint8_t trampoline_end;
extern char off_cpu_id[];
extern char off_stack[];
extern char off_cr3[];
extern void put_u64(uint64_t v);
extern volatile uint32_t *lapic;
#define TRAMP_PHYS 0x7000ULL
#define MARKER     0x7FF0

volatile uint32_t ap_started = 0;

static inline uint32_t lap_read(uint32_t r) { return lapic[r >> 2]; }
static inline void lap_write(uint32_t r, uint32_t v) { lapic[r >> 2] = v; }

static void lap_wait(void) {
    for (volatile int i = 0; i < 1000000; i++)
        if (!(lap_read(0x300) & (1 << 12))) return;
}

void smp_ap_c_entry(uint32_t cpu_id) {
    __asm__ volatile("cli");
    *(volatile uint32_t *)MARKER = 0x1000000A;
    ap_started = cpu_id;
    for (;;) __asm__ volatile("hlt");
}

static void report(uint32_t m) {
    switch (m) {
        case 0x10000001: con_puts("[SMP] died in lgdt/lidt\n"); break;
        case 0x10000002: con_puts("[SMP] died at CR0.PE\n"); break;
        case 0x10000003: con_puts("[SMP] died in real->32 far jump\n"); break;
        case 0x10000004: con_puts("[SMP] died in 32-bit segs / CR4 / temp CR3\n"); break;
        case 0x10000005: con_puts("[SMP] died at EFER\n"); break;
        case 0x10000006: con_puts("[SMP] died at CR0.PG\n"); break;
        case 0x10000007: con_puts("[SMP] died in 32->64 far jump\n"); break;
        case 0x10000008: con_puts("[SMP] died reading cpu_id / stack / cr3\n"); break;
        case 0x10000009: con_puts("[SMP] died at kernel CR3 switch or C entry\n"); break;
        case 0x1000000A: con_puts("[SMP] SUCCESS AP is in C!\n"); break;
        default:         con_puts("[SMP] AP never started\n"); break;
    }
}

void smp_init(void) {
    con_puts("[SMP] Initializing...\n");
    size_t n = &trampoline_end - &trampoline_start;
    memcpy((void *)TRAMP_PHYS, &trampoline_start, n);
    uint64_t *pml4 = (uint64_t *)0x8000;
    uint64_t *pdpt = (uint64_t *)0x9000;
    uint64_t *pd   = (uint64_t *)0xA000;
    memset(pml4, 0, 4096);
    memset(pdpt, 0, 4096);
    memset(pd,   0, 4096);
    pml4[0] = 0x9000 | 3;
    pdpt[0] = 0xA000 | 3;
    pd[0]   = 0x0000 | 0x83;

    uint64_t kcr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(kcr3));

    for (uint32_t i = 1; i < (uint32_t)acpi.cpu_count; i++) {
        uint32_t apic = acpi.cpu_apic_ids[i];

        con_puts("[SMP] Waking CPU "); put_u64(i);
        con_puts(" (APIC "); put_u64(apic); con_puts(")\n");

        *(volatile uint32_t *)(TRAMP_PHYS + (uintptr_t)off_cpu_id) = i;
        *(volatile uint64_t *)(TRAMP_PHYS + (uintptr_t)off_stack)  = gdt_get_ap_stack(i);
        *(volatile uint64_t *)(TRAMP_PHYS + (uintptr_t)off_cr3)    = kcr3;
        *(volatile uint32_t *)MARKER = 0;
        ap_started = 0;

        lap_write(0x310, apic << 24);
        lap_write(0x300, 0x00004500);
        lap_wait();
        for (volatile int j = 0; j < 1000000; j++);

        lap_write(0x310, apic << 24);
        lap_write(0x300, 0x00008500);
        lap_wait();
        for (volatile int j = 0; j < 1000000; j++);

        for (int s = 0; s < 2; s++) {
            lap_write(0x310, apic << 24);
            lap_write(0x300, 0x00000607);
            lap_wait();
            for (volatile int j = 0; j < 20000; j++);
        }

        for (volatile int j = 0; j < 50000000; j++)
            if (ap_started == i) break;

        report(*(volatile uint32_t *)MARKER);
    }
    con_puts("[SMP] done.\n");
}
