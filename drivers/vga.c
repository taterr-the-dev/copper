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

static void vga_update_cursor(void) {
  uint16_t pos = row * VGA_W + col;
  outb(0x3D4, 0x0F);
  outb(0x3D5, (uint8_t)(pos & 0xFF));
  outb(0x3D4, 0x0E);
  outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

static uint8_t vga_color = 0x0F;
static int ansi_state = 0;

static uint8_t get_ansi_vga_color(int code) {
    switch (code) {
        case 30: case 40: return 0;
        case 31: case 41: return 4;
        case 32: case 42: return 2;
        case 33: case 43: return 6;
        case 34: case 44: return 1;
        case 35: case 45: return 5;
        case 36: case 46: return 3;
        case 37: case 47: return 7;
        case 90: case 100: return 8;
        case 91: case 101: return 12;
        case 92: case 102: return 10;
        case 93: case 103: return 14;
        case 94: case 104: return 9;
        case 95: case 105: return 13;
        case 96: case 106: return 11;
        case 97: case 107: return 15;
        default: return 7;
    }
}

static int ansi_params[4];
static int ansi_pidx = 0;
static int ansi_cur = 0;

static uint16_t entry(char c) { return (uint16_t)c | ((uint16_t)vga_color << 8); }
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
    if (c == '\x1b') { ansi_state = 1; return; }
  } else if (ansi_state == 1) {
    if (c == '[') { ansi_state = 2; ansi_pidx = 0; ansi_cur = 0; ansi_params[0] = 0; return; }
    if (c == '?') { ansi_state = 3; return; }
    ansi_state = 0;
  } else if (ansi_state == 2) {
    if (c >= '0' && c <= '9') { ansi_cur = ansi_cur * 10 + (c - '0'); return; }
    if (c == ';') { if (ansi_pidx < 4) ansi_params[ansi_pidx++] = ansi_cur; ansi_cur = 0; return; }
    
    if (c == 'm') {
      int codes[5]; int count = 0;
      for(int i=0; i<ansi_pidx; i++) codes[count++] = ansi_params[i];
      codes[count++] = ansi_cur;
      
      for (int i = 0; i < count; i++) {
        int p = codes[i];
        if (p == 0) vga_color = 0x0F;
        else if (p == 1) vga_color |= 0x08; 
        else if ((p >= 30 && p <= 37) || (p >= 90 && p <= 97)) {
            vga_color = (vga_color & 0xF0) | get_ansi_vga_color(p);
        }
        else if ((p >= 40 && p <= 47) || (p >= 100 && p <= 107)) {
            vga_color = (vga_color & 0x0F) | (get_ansi_vga_color(p) << 4);
        }
      }
      ansi_state = 0;
      return;
    }
    
    if (c == 'H') {
      int r = (ansi_pidx > 0) ? ansi_params[0] - 1 : 0;
      int c_pos = ansi_cur - 1;
      if (ansi_pidx == 0) { r = 0; c_pos = 0; }
      if (r >= 0 && r < VGA_H) row = r;
      if (c_pos >= 0 && c_pos < VGA_W) col = c_pos;
      vga_update_cursor();
      ansi_state = 0;
      return;
    }
    
    if (c == 'J') {
      if (ansi_cur == 2 || (ansi_pidx > 0 && ansi_params[0] == 2)) {
        for (int i = 0; i < VGA_W * VGA_H; i++) vga_mem[i] = entry(' ');
        row = 0; col = 0; vga_update_cursor();
      }
      ansi_state = 0;
      return;
    }
    
    if (c == 'K') {
      for (int i = col; i < VGA_W; i++) vga_mem[row * VGA_W + i] = entry(' ');
      ansi_state = 0;
      return;
    }
    
    ansi_state = 0;
  } else if (ansi_state == 3) {
    if (c >= 'a' && c <= 'z') ansi_state = 0;
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
