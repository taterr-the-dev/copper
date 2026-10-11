#include <kernel/types.h>
#include <kernel/string.h>
#include <kernel/fbcon.h>
#include <kernel/font8x8.h>

#define CHAR_W 8
#define CHAR_H 8
#define CHAR_STEP 9
#define LINE_STEP 11

static uint8_t *fb_base = 0;
static uint64_t fb_width = 0;
static uint64_t fb_height = 0;
static uint64_t fb_pitch = 0;
static int fb_cx = 0;
static int fb_cy = 0;
static int fb_ready = 0;

static uint32_t fg_color = 0xFFFFFFFF;
static uint32_t bg_color = 0x00000000;
static int ansi_state = 0;
static int ansi_param = 0;

static uint32_t get_ansi_color(int code, int is_bg) {
  static const uint32_t std_colors[8] = {
    0xFF000000, 0xFFFF0000, 0xFF00FF00, 0xFFFFFF00,
    0xFF0000FF, 0xFFFF00FF, 0xFF00FFFF, 0xFFFFFFFF
  };
  static const uint32_t bright_colors[8] = {
    0xFF808080, 0xFFFF8080, 0xFF80FF80, 0xFFFFFF80,
    0xFF8080FF, 0xFFFF80FF, 0xFF80FFFF, 0xFFFFFFFF
  };

  if (code >= 30 && code <= 37) return std_colors[code - 30];
  if (code >= 40 && code <= 47) return std_colors[code - 40];
  if (code >= 90 && code <= 97) return bright_colors[code - 90];
  if (code >= 100 && code <= 107) return bright_colors[code - 100];
  return is_bg ? 0x00000000 : 0xFFFFFFFF;
}

void fbcon_init(uint64_t va, uint64_t width, uint64_t height, uint64_t pitch) {
  fb_base = (uint8_t *)va;
  fb_width = width;
  fb_height = height;
  fb_pitch = pitch;
  memset(fb_base, 0, fb_pitch * fb_height);
  fb_ready = 1;
  fb_cx = 0;
  fb_cy = 0;
}

void fb_draw_pixel(int x, int y, uint32_t color) {
  if (x < 0 || x >= (int)fb_width || y < 0 || y >= (int)fb_height) return;
  uint8_t *pixel = fb_base + (y * fb_pitch) + (x * 4);
  pixel[0] = color & 0xFF;
  pixel[1] = (color >> 8) & 0xFF;
  pixel[2] = (color >> 16) & 0xFF;
  pixel[3] = (color >> 24) & 0xFF;
}

void fb_draw_char(char c, int x, int y, uint32_t fg, uint32_t bg) {
  const uint8_t *glyph = get_font_glyph(c);
  for (int row = 0; row < 8; row++) {
    for (int col = 0; col < 8; col++) {
      fb_draw_pixel(x + col, y + row, bg);
    }
  }
  for (int row = 0; row < 8; row++) {
    uint8_t line = glyph[row];
    for (int col = 0; col < 8; col++) {
      if (line & (1 << col)) {
        fb_draw_pixel(x + col, y + row, fg);
      }
    }
  }
}

void fb_scroll() {
  memmove(fb_base, fb_base + (LINE_STEP * fb_pitch), (fb_height - LINE_STEP) * fb_pitch);
  memset(fb_base + ((fb_height - LINE_STEP) * fb_pitch), 0, LINE_STEP * fb_pitch);
}

void fbcon_putc(char c) {
  if (!fb_ready) return;

  if (ansi_state == 0) {
    if (c == '\033') { ansi_state = 1; return; }
  } else if (ansi_state == 1) {
    if (c == '[') { ansi_state = 2; ansi_param = 0; return; }
    ansi_state = 0;
  } else if (ansi_state == 2) {
    if (c >= '0' && c <= '9') { ansi_param = ansi_param * 10 + (c - '0'); return; }
    
    if (c == 'm') {
      if (ansi_param == 0) { fg_color = 0xFFFFFFFF; bg_color = 0x00000000; }
      else if ((ansi_param >= 30 && ansi_param <= 37) || (ansi_param >= 90 && ansi_param <= 97)) fg_color = get_ansi_color(ansi_param, 0);
      else if ((ansi_param >= 40 && ansi_param <= 47) || (ansi_param >= 100 && ansi_param <= 107)) bg_color = get_ansi_color(ansi_param, 1);
      ansi_state = 0;
      return;
    }
    
    if (c == 'J') { if (ansi_param == 2) { memset(fb_base, 0, fb_pitch * fb_height); fb_cx = 0; fb_cy = 0; } ansi_state = 0; return; }
    if (c == 'H') { fb_cx = 0; fb_cy = 0; ansi_state = 0; return; }
    if (c == 'A') { fb_cy -= ansi_param * LINE_STEP; if (fb_cy < 0) fb_cy = 0; ansi_state = 0; return; }
    if (c == 'B') { fb_cy += ansi_param * LINE_STEP; ansi_state = 0; return; }
    if (c == 'C') { fb_cx += ansi_param * CHAR_STEP; ansi_state = 0; return; }
    if (c == 'D') { fb_cx -= ansi_param * CHAR_STEP; if (fb_cx < 0) fb_cx = 0; ansi_state = 0; return; }
    
    ansi_state = 0;
    return; 
  }

  if (c == '\n') {
    fb_cx = 0;
    fb_cy += LINE_STEP;
  } else if (c == '\r') {
    fb_cx = 0;
  } else if (c == '\b') {
    fb_cx -= CHAR_STEP;
    if (fb_cx < 0) { fb_cx = fb_width - CHAR_STEP; fb_cy -= LINE_STEP; }
    if (fb_cy < 0) fb_cy = 0;
    fb_draw_char(' ', fb_cx, fb_cy, bg_color, bg_color);
  } else {
    fb_draw_char(c, fb_cx, fb_cy, fg_color, bg_color);
    fb_cx += CHAR_STEP;
    if (fb_cx + CHAR_W > (int)fb_width) {
      fb_cx = 0;
      fb_cy += LINE_STEP;
    }
  }

  if (fb_cy + LINE_STEP > (int)fb_height) {
    fb_scroll();
    fb_cy -= LINE_STEP;
  }
}
