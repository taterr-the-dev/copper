#include <kernel/percpu.h>
#include <kernel/pmm.h>
#include <kernel/msr.h>
#include <kernel/console.h>
#include <kernel/string.h>

#define MAX_CPUS 16
static struct percpu percpu_areas[MAX_CPUS] __attribute__((aligned(16)));

void percpu_init_bsp(void) {
    memset(&percpu_areas[0], 0, sizeof(struct percpu));
    percpu_areas[0].cpu_id = 0;
    percpu_areas[0].apic_id = 0;
    percpu_areas[0].current = NULL;
    wrmsr(MSR_GS_BASE, (uint64_t)&percpu_areas[0]);
    con_puts("[PERCPU] BSP per-CPU area initialized.\n");
}

struct percpu *percpu_get_area(uint32_t cpu_id) {
    if (cpu_id >= MAX_CPUS) return NULL;
    return &percpu_areas[cpu_id];
}
