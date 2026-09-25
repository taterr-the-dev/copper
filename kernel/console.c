#include <autoconf.h>
#include <kernel/console.h>
#ifdef CONFIG_SERIAL_UART
#include <drivers/serial.h>
#endif
#ifdef CONFIG_VGA_TEXT
#include <drivers/vga.h>
#endif

static inline uint64_t irq_save(void) {
  uint64_t f;
  __asm__ volatile("pushfq; pop %0; cli" : "=r"(f));
  return f;
}
static inline void irq_restore(uint64_t f) {
  __asm__ volatile("push %0; popfq" ::"r"(f) : "memory", "cc");
}

void con_init(void) {
#ifdef CONFIG_SERIAL_UART
  serial_init();
#endif
#ifdef CONFIG_VGA_TEXT
  vga_init();
#endif
}
void con_putc(char c) {
  uint64_t f = irq_save();
#ifdef CONFIG_SERIAL_UART
  serial_putc(c);
#endif
#ifdef CONFIG_VGA_TEXT
  vga_putc(c);
#endif
  irq_restore(f);
}
void con_puts(const char *s) {
  while (*s)
    con_putc(*s++);
}
