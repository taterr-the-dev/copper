#include <kernel/interrupts.h>
#include <kernel/io.h>
volatile uint64_t timer_ticks = 0;
void pit_init(uint32_t hz) {
  uint32_t div = 1193180 / hz;
  outb(0x43, 0x36);
  outb(0x40, div & 0xFF);
  outb(0x40, (div >> 8) & 0xFF);
}
void timer_tick(void) { timer_ticks++; }
