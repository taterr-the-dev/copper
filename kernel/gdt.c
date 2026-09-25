#include <kernel/console.h>
#include <kernel/types.h>

struct tss {
  uint32_t rsv0;
  uint64_t rsp0, rsp1, rsp2;
  uint64_t rsv1;
  uint64_t ist[7];
  uint64_t rsv2;
  uint16_t rsv3;
  uint16_t iopb;
} __attribute__((packed));

static struct tss tss;
static uint8_t int_stack[16384] __attribute__((aligned(16)));

static uint64_t gdt[8] = {0x0000000000000000,
                          0x00AF9A000000FFFF,
                          0x00CF92000000FFFF,
                          0x0000000000000000,
                          0x00CFF2000000FFFF,
                          0x00AFFA000000FFFF,
                          0,
                          0};
struct gdtp {
  uint16_t limit;
  uint64_t base;
} __attribute__((packed));
static struct gdtp gp;

void gdt_set_kernel_stack(uint64_t rsp0) { tss.rsp0 = rsp0; }

void gdt_init(void) {
  uint64_t base = (uint64_t)&tss;
  uint32_t limit = sizeof(tss) - 1;
  uint64_t d = 0;
  d |= (limit & 0xFFFF);
  d |= (base & 0xFFFFFF) << 16;
  d |= (uint64_t)0x89 << 40;
  d |= ((uint64_t)(limit >> 16) & 0xF) << 48;
  d |= ((base >> 24) & 0xFF) << 56;
  gdt[6] = d;
  gdt[7] = (base >> 32) & 0xFFFFFFFF;

  tss.rsp0 = (uint64_t)(int_stack + sizeof(int_stack));
  tss.iopb = sizeof(tss);

  gp.limit = sizeof(gdt) - 1;
  gp.base = (uint64_t)gdt;
  __asm__ volatile("lgdt %0" ::"m"(gp));
  __asm__ volatile(
      "mov $0x10,%%ax; mov %%ax,%%ds; mov %%ax,%%es; mov %%ax,%%ss" ::
          : "rax");
  __asm__ volatile("ltr %0" ::"r"((uint16_t)0x30));
  con_puts("[OK] GDT + TSS initialized (user segments present).\n");
}
