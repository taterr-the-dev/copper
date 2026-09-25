#include <autoconf.h>
#ifdef CONFIG_SERIAL_UART
#include <drivers/serial.h>
#include <kernel/io.h>

static const uint16_t com_ports[4] = {0x3F8, 0x2F8, 0x3E8, 0x2E8};

#ifndef CONFIG_SERIAL_PORT
#define CONFIG_SERIAL_PORT 0
#endif
#ifndef CONFIG_SERIAL_BAUD
#define CONFIG_SERIAL_BAUD 115200
#endif

static uint16_t port(void) { return com_ports[CONFIG_SERIAL_PORT & 3]; }

void serial_init(void) {
  uint16_t p = port();
  uint16_t div = 115200 / CONFIG_SERIAL_BAUD;
  outb(p + 1, 0x00);
  outb(p + 3, 0x80);
  outb(p + 0, div & 0xFF);
  outb(p + 1, (div >> 8) & 0xFF);
  outb(p + 3, 0x03);
  outb(p + 2, 0xC7);
  outb(p + 4, 0x0B);
}

static int tx_empty(void) { return inb(port() + 5) & 0x20; }

void serial_putc(char c) {
  while (!tx_empty())
    ;
  outb(port(), c);
}

void serial_puts(const char *s) {
  while (*s) {
    if (*s == '\n')
      serial_putc('\r');
    serial_putc(*s++);
  }
}
#endif
