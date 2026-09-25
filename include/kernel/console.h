#ifndef _KERNEL_CONSOLE_H
#define _KERNEL_CONSOLE_H
void con_init(void);
void con_putc(char c);
void con_puts(const char *s);
#endif
