#include <autoconf.h>
#include <kernel/console.h>
#include <kernel/arch.h>
#include <kernel/fbcon.h>
#ifdef CONFIG_SERIAL_UART
#include <drivers/serial.h>
#endif
#ifdef CONFIG_VGA_TEXT
#include <drivers/vga.h>
#endif
#include <kernel/spinlock.h>

static inline uint64_t irq_save(void) {
	uint64_t f = arch_save_flags();
	arch_cli();
  return f;
}
static inline void irq_restore(uint64_t f) {
	arch_restore_flags(f);
}

static spinlock_t console_lock = SPINLOCK_INIT;

void con_init(void) {
#ifdef CONFIG_SERIAL_UART
  serial_init();
#endif
#ifdef CONFIG_VGA_TEXT
  vga_init();
#endif
  spinlock_init(&console_lock);
}
void con_putc(char c) {
  uint64_t flags;
  spin_lock_irqsave(&console_lock, &flags);
  uint64_t f = irq_save();
#ifdef CONFIG_SERIAL_UART
  serial_putc(c);
#endif
#ifdef CONFIG_VGA_TEXT
  vga_putc(c);
#endif
  fbcon_putc(c);
  irq_restore(f);
  spin_unlock_irqrestore(&console_lock, flags);
}
void con_puts(const char *s) {
  while (*s)
    con_putc(*s++);
}
