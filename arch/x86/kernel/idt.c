#include <kernel/interrupts.h>
#include <kernel/string.h>
struct idt_entry {
  uint16_t base_lo;
  uint16_t sel;
  uint8_t ist;
  uint8_t type;
  uint16_t base_mid;
  uint32_t base_hi;
  uint32_t rsv;
};
struct idt_ptr {
  uint16_t limit;
  uint64_t base;
} __attribute__((packed));
static struct idt_entry idt[256];
static struct idt_ptr idtp;
extern uint64_t isr_stub_table[];
static void set_gate(uint8_t n, uint64_t base) {
  idt[n].base_lo = base & 0xFFFF;
  idt[n].sel = 0x08;
  idt[n].ist = 0;
  idt[n].type = 0x8E;
  idt[n].base_mid = (base >> 16) & 0xFFFF;
  idt[n].base_hi = (base >> 32) & 0xFFFFFFFF;
  idt[n].rsv = 0;
}
void idt_install(void) {
  memset(idt, 0, sizeof(idt));
  for (int i = 0; i < 48; i++)
    set_gate(i, isr_stub_table[i]);
  idtp.limit = sizeof(idt) - 1;
  idtp.base = (uint64_t)idt;
  __asm__ volatile("lidt %0" ::"m"(idtp));
}

void idt_init(void) { idt_install(); }
