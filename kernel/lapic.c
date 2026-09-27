#include <autoconf.h>
#include <kernel/console.h>
#include <kernel/msr.h>
#include <kernel/vm.h>
#include <kernel/pmm.h>
#include <kernel/io.h>

#define LAPIC_BASE_MSR 0x1B
#define LAPIC_BASE_ADDR 0xFEE00000ULL

#define LAPIC_ID       0x020
#define LAPIC_EOI      0x0B0
#define LAPIC_SPURIOUS 0x0F0

#define LAPIC_LVT_TIMER   0x320
#define LAPIC_TIMER_INIT  0x380
#define LAPIC_TIMER_DIV   0x3E0

#define LAPIC_LVT_LINT0 0x350
#define LAPIC_LVT_LINT1 0x360

#define LAPIC_TIMER_CURR  0x390

extern void put_u64(uint64_t v);

static volatile uint32_t *lapic = NULL;

static inline uint32_t lapic_read(uint32_t reg) {
  return lapic[reg >> 2];
}

static inline void lapic_write(uint32_t reg, uint32_t val) {
  lapic[reg >> 2] = val;
}

void lapic_init(void) {
  uint64_t apic_base = rdmsr(LAPIC_BASE_MSR);
  apic_base |= (1ULL << 11);
  wrmsr(LAPIC_BASE_MSR, apic_base);
  vm_map((uint64_t *)boot_cr3, LAPIC_BASE_ADDR, LAPIC_BASE_ADDR, 0x1B);
  uint64_t cr3;
  __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
  __asm__ volatile("mov %0, %%cr3" :: "r"(cr3) : "memory");
  lapic = (volatile uint32_t *)LAPIC_BASE_ADDR;
  uint32_t spurious = lapic_read(LAPIC_SPURIOUS);
  spurious |= (1 << 8);
  spurious |= 0xFF;
  lapic_write(LAPIC_SPURIOUS, spurious);
  lapic_write(LAPIC_LVT_LINT0, 0x700);
  lapic_write(LAPIC_LVT_LINT1, 0x10000);
  con_puts("[LAPIC] Initialized and enabled.\n");
}

void lapic_timer_init(void) {
  outb(0x61, (inb(0x61) & ~0x02) | 0x01);
  outb(0x43, 0xB0);
  outb(0x42, 11931 & 0xFF);
  outb(0x42, (11931 >> 8) & 0xFF);
  lapic_write(LAPIC_TIMER_DIV, 0x03);
  lapic_write(LAPIC_LVT_TIMER, 0x20);
  lapic_write(LAPIC_TIMER_INIT, 0xFFFFFFFF);
  while (!(inb(0x61) & 0x20));
  uint32_t elapsed = 0xFFFFFFFF - lapic_read(LAPIC_TIMER_CURR);
  lapic_write(LAPIC_LVT_TIMER, 0x20020);
  lapic_write(LAPIC_TIMER_INIT, elapsed);
  con_puts("[LAPIC] Timer calibrated and started.\n");
}

void lapic_send_eoi(void) {
  if (lapic) {
    lapic_write(LAPIC_EOI, 0);
  }
}
