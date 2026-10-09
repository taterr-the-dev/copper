#include <autoconf.h>
#ifdef CONFIG_VGA_TEXT
#include <drivers/vga.h>
#include <kernel/types.h>
#include <kernel/io.h>

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

static void vga_update_cursor(void) {
  uint16_t pos = row * VGA_W + col;
  outb(0x3D4, 0x0F);
  outb(0x3D5, (uint8_t)(pos & 0xFF));
  outb(0x3D4, 0x0E);
  outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

static int ansi_state = 0;
static int ansi_params[4];
static int ansi_pidx = 0;
static int ansi_cur = 0;

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
  vga_update_cursor();
}
void vga_putc(char c) {
  if (ansi_state == 0) {
    if (c == '\x1b') {
      ansi_state = 1;
      return;
    }
  } else if (ansi_state == 1) {
    if (c == '[') {
      ansi_state = 2;
      ansi_pidx = 0;
      ansi_cur = 0;
      ansi_params[0] = 0;
      return;
    }
    ansi_state = 0;
  } else if (ansi_state == 2) {
    if (c >= '0' && c <= '9') {
      ansi_cur = ansi_cur * 10 + (c - '0');
      return;
    } else if (c == ';') {
      if (ansi_pidx < 4) ansi_params[ansi_pidx++] = ansi_cur;
      ansi_cur = 0;
      return;
    } else if (c == 'H') {
      int r = (ansi_pidx > 0 && ansi_params[0] > 0) ? ansi_params[0] - 1 : 0;
      int c_pos = (ansi_pidx > 1 && ansi_params[1] > 0) ? ansi_params[1] - 1 : 0;
      if (r >= 0 && r < VGA_H) row = r;
      if (c_pos >= 0 && c_pos < VGA_W) col = c_pos;
      vga_update_cursor();
      ansi_state = 0;
      return;
    } else if (c == 'J') {
      if (ansi_params[0] == 2 || ansi_cur == 2) {
        for (int i = 0; i < VGA_W * VGA_H; i++) vga_mem[i] = entry(' ');
        row = 0; col = 0;
        vga_update_cursor();
      }
      ansi_state = 0;
      return;
    } else if (c == 'K') {
      for (int i = col; i < VGA_W; i++) vga_mem[row * VGA_W + i] = entry(' ');
      ansi_state = 0;
      return;
    } else if (c == 'm') {
      ansi_state = 0;
      return;
    } else if (c == '?') {
      ansi_state = 3;
      return;
    }
    ansi_state = 0;
  } else if (ansi_state == 3) {
    if (c >= 'a' && c <= 'z') {
      ansi_state = 0;
    }
    return;
  }

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
  } else if (c >= 32) {
    vga_mem[row * VGA_W + col] = entry(c);
    if (++col == VGA_W) {
      col = 0;
      row++;
    }
  }

  if (row == VGA_H)
    scroll();
  vga_update_cursor();
}
void vga_puts(const char *s) {
  while (*s)
    vga_putc(*s++);
}
#endif
