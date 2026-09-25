#include <autoconf.h>
#ifdef CONFIG_VGA_TEXT
#include <drivers/vga.h>
#include <kernel/types.h>

#ifndef CONFIG_VGA_WIDTH
#define CONFIG_VGA_WIDTH 80
#endif
#ifndef CONFIG_VGA_HEIGHT
#define CONFIG_VGA_HEIGHT 25
#endif
#define VGA_W CONFIG_VGA_WIDTH
#define VGA_H CONFIG_VGA_HEIGHT

static volatile uint16_t *vga_mem = (volatile uint16_t *)0xB8000;
static int row = 0, col = 0;
static const uint8_t COLOR = 0x07;

static uint16_t entry(char c) { return (uint16_t)c | ((uint16_t)COLOR << 8); }
static void scroll(void) {
  for (int i = 0; i < VGA_W * (VGA_H - 1); i++)
    vga_mem[i] = vga_mem[i + VGA_W];
  for (int i = 0; i < VGA_W; i++)
    vga_mem[VGA_W * (VGA_H - 1) + i] = entry(' ');
  row = VGA_H - 1;
}
void vga_init(void) {
  for (int i = 0; i < VGA_W * VGA_H; i++)
    vga_mem[i] = entry(' ');
  row = 0;
  col = 0;
}
void vga_putc(char c) {
  if (c == '\n') {
    col = 0;
    row++;
  } else if (c == '\r') {
    col = 0;
  } else if (c == '\b') {
    if (col > 0) {
      col--;
      vga_mem[row * VGA_W + col] = entry(' ');
    } else if (row > 0) {
      row--;
      col = VGA_W - 1;
      vga_mem[row * VGA_W + col] = entry(' ');
    }
  } else {
    vga_mem[row * VGA_W + col] = entry(c);
    if (++col == VGA_W) {
      col = 0;
      row++;
    }
  }
  if (row == VGA_H)
    scroll();
}
void vga_puts(const char *s) {
  while (*s)
    vga_putc(*s++);
}
#endif
