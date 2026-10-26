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

DEFINE_SPINLOCK(console_lock);

void con_init(void) {
#ifdef CONFIG_SERIAL_UART
  serial_init();
#endif
#ifdef CONFIG_VGA_TEXT
  vga_init();
#endif
  spinlock_init(&console_lock);
}
static void __con_putc(char c) {
#ifdef CONFIG_SERIAL_UART
  serial_putc(c);
#endif
#ifdef CONFIG_VGA_TEXT
  vga_putc(c);
#endif
  fbcon_putc(c);
}
void con_putc(char c) {
  uint64_t flags;
  spin_lock_irqsave(&console_lock, &flags);
  __con_putc(c);
  spin_unlock_irqrestore(&console_lock, flags);
}

void con_puts(const char *s) {
  uint64_t flags;
  spin_lock_irqsave(&console_lock, &flags);
  while (*s)
    __con_putc(*s++);
  spin_unlock_irqrestore(&console_lock, flags);
}

void console_bust_lock(void) { console_lock.locked = 0; }
