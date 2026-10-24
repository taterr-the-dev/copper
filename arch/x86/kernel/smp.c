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

volatile uint32_t ap_started = 0;

static inline uint32_t lap_read(uint32_t r) { return lapic[r >> 2]; }
static inline void lap_write(uint32_t r, uint32_t v) { lapic[r >> 2] = v; }

static void lap_wait(void) {
    for (volatile int i = 0; i < 1000000; i++)
        if (!(lap_read(0x300) & (1 << 12))) return;
}

static void ap_lapic_init(void) {
    lap_write(0xF0, 0x1FF);
    lap_write(0x320, 0x10000);
    lap_write(0x330, 0x10000);
    lap_write(0x340, 0x10000);
    lap_write(0x350, 0x10000);
    lap_write(0x360, 0x10000);
    lap_write(0x370, 0x10000);
    lap_write(0x80, 0);
}

void smp_ap_c_entry(uint32_t cpu_id) {
    __asm__ volatile("cli");
    extern void gdt_init_cpu(uint32_t cpu_id);
    extern void idt_init(void);
    gdt_init_cpu(cpu_id);
    idt_init();
    ap_lapic_init();
    extern struct percpu *percpu_get_area(uint32_t cpu_id);
    struct percpu *my_percpu = percpu_get_area(cpu_id);
    if (my_percpu) {
        uint64_t gs_base = (uint64_t)my_percpu + 0xFFFF800000000000ULL;
        uint32_t lo = (uint32_t)(gs_base & 0xFFFFFFFF);
        uint32_t hi = (uint32_t)(gs_base >> 32);
        __asm__ volatile("wrmsr" :: "c"(0xC0000101), "a"(lo), "d"(hi));
    }
    con_puts("[SMP] CPU ");
    put_u64(cpu_id);
    con_puts(" online.\n");
    ap_started = cpu_id;
    __asm__ volatile("sti");
    for (;;) __asm__ volatile("hlt");
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
        *(volatile uint32_t *)(TRAMP_PHYS + (uintptr_t)off_cpu_id) = i;
        *(volatile uint64_t *)(TRAMP_PHYS + (uintptr_t)off_stack)  = gdt_get_ap_stack(i);
        *(volatile uint64_t *)(TRAMP_PHYS + (uintptr_t)off_cr3)    = kcr3;
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

        if (ap_started != i) {
                con_puts("[SMP] Failed to start CPU ");
                put_u64(i);
                con_puts("\n");
            }
    }
    con_puts("[SMP] done.\n");
}
