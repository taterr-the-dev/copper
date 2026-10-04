#include <kernel/interrupts.h>
#include <kernel/io.h>
#include <kernel/console.h>
#include <kernel/proc.h>

volatile uint64_t timer_ticks = 0;
extern void put_u64(uint64_t v);

void pit_init(uint32_t hz) {
  uint32_t div = 1193180 / hz;
  outb(0x43, 0x36);
  outb(0x40, div & 0xFF);
  outb(0x40, (div >> 8) & 0xFF);
}

void timer_tick(void) {
  timer_ticks++;
  extern struct task *current;
  if (current) {
    struct task *t = current->next;
    struct task *start = t;
    do {
      if (t->state == T_BLOCKED && t->sleep_until > 0 && timer_ticks >= t->sleep_until) {
        t->state = T_READY;
        t->sleep_until = 0;
      }
      t = t->next;
    } while (t != start);
  }
}
