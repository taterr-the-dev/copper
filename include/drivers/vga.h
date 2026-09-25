#ifndef _KERNEL_VGA_H
#define _KERNEL_VGA_H
void vga_init(void);
void vga_putc(char c);
void vga_puts(const char *s);
#endif
