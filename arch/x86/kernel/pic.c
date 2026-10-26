#include <kernel/interrupts.h>
#include <kernel/io.h>
#include <kernel/spinlock.h>
#define P1 0x20
#define P2 0xA0
DEFINE_SPINLOCK(pic_lock);
void pic_remap(uint32_t o1, uint32_t o2) {
  outb(P1, 0x11);
  outb(P2, 0x11);
  outb(P1 + 1, o1);
  outb(P2 + 1, o2);
  outb(P1 + 1, 4);
  outb(P2 + 1, 2);
  outb(P1 + 1, 1);
  outb(P2 + 1, 1);
  outb(P1 + 1, 0xFF);
  outb(P2 + 1, 0xFF);
}
void pic_set_mask(uint8_t irq) {
  uint16_t p = irq < 8 ? P1 + 1 : P2 + 1;
  uint8_t b = irq & 7;
  uint64_t flags;
  spin_lock_irqsave(&pic_lock, &flags);
  outb(p, inb(p) | (1 << b));
  spin_unlock_irqrestore(&pic_lock, flags);
}
void pic_clear_mask(uint8_t irq) {
  uint16_t p = irq < 8 ? P1 + 1 : P2 + 1;
  uint8_t b = irq & 7;
  uint64_t flags;
  spin_lock_irqsave(&pic_lock, &flags);
  outb(p, inb(p) & ~(1 << b));
  spin_unlock_irqrestore(&pic_lock, flags);
}
void pic_sendEOI(uint8_t irq) {
  if (irq >= 8)
    outb(P2, 0x20);
  outb(P1, 0x20);
}
