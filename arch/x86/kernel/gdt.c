#include <kernel/console.h>
#include <kernel/types.h>
#include <kernel/percpu.h>

struct tss {
    uint32_t rsv0;
    uint64_t rsp0, rsp1, rsp2;
    uint64_t rsv1;
    uint64_t ist[7];
    uint64_t rsv2;
    uint16_t rsv3;
    uint16_t iopb;
} __attribute__((packed));

struct gdtp {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

#define MAX_CPUS 16

static struct tss percpu_tss[MAX_CPUS];
static uint8_t percpu_int_stack[MAX_CPUS][16384] __attribute__((aligned(16)));
static uint64_t percpu_gdt[MAX_CPUS][8];
static struct gdtp percpu_gp[MAX_CPUS];
static const uint64_t template_gdt[6] = {
    0x0000000000000000,
    0x00AF9A000000FFFF,
    0x00CF92000000FFFF,
    0x0000000000000000,
    0x00CFF2000000FFFF,
    0x00AFFA000000FFFF
};

void gdt_init_cpu(uint32_t cpu_id) {
    if (cpu_id >= MAX_CPUS) return;
    struct tss *my_tss = &percpu_tss[cpu_id];
    uint64_t *my_gdt = percpu_gdt[cpu_id];
    struct gdtp *my_gp = &percpu_gp[cpu_id];
    uint8_t *my_stack = percpu_int_stack[cpu_id];
    for (int i = 0; i < 6; i++) my_gdt[i] = template_gdt[i];
    uint64_t base = (uint64_t)my_tss;
    uint32_t limit = sizeof(struct tss) - 1;
    uint64_t d = 0;
    d |= (limit & 0xFFFF);
    d |= (base & 0xFFFFFF) << 16;
    d |= (uint64_t)0x89 << 40;
    d |= ((uint64_t)(limit >> 16) & 0xF) << 48;
    d |= ((base >> 24) & 0xFF) << 56;
    my_gdt[6] = d;
    my_gdt[7] = (base >> 32) & 0xFFFFFFFF;
    my_tss->rsp0 = (uint64_t)(my_stack + 16384);
    my_tss->rsp1 = 0;
    my_tss->rsp2 = 0;
    my_tss->iopb = sizeof(struct tss);
    my_gp->limit = sizeof(uint64_t) * 8 - 1;
    my_gp->base = (uint64_t)my_gdt;

    __asm__ volatile("lgdt %0" ::"m"(*my_gp));
    __asm__ volatile(
        "mov $0x10,%%ax; mov %%ax,%%ds; mov %%ax,%%es; mov %%ax,%%ss" ::
        : "rax");
    __asm__ volatile("ltr %0" ::"r"((uint16_t)0x30));
    percpu_write(gdt_ptr, my_gdt);
    percpu_write(tss_ptr, my_tss);
    percpu_write(kernel_stack, my_tss->rsp0);
}

void gdt_init(void) {
    gdt_init_cpu(0);
    con_puts("[OK] GDT + TSS initialized (user segments present).\n");
}

void gdt_set_kernel_stack(uint64_t rsp0) {
    struct tss *my_tss = percpu_read(tss_ptr);
    if (my_tss) {
        my_tss->rsp0 = rsp0;
    }
}
