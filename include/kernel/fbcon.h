#ifndef KERNEL_FBCON_H
#define KERNEL_FBCON_H

#include <kernel/types.h>

void fbcon_init(uint64_t va, uint64_t width, uint64_t height, uint64_t pitch);
void fb_draw_pixel(int x, int y, uint32_t color);
void fb_draw_char(char c, int x, int y, uint32_t fg, uint32_t bg);
void fb_draw_string(const char *str, int x, int y, uint32_t color);
void fbcon_putc(char c);

#endif
