#ifndef _KERNEL_SERIAL_H
#define _KERNEL_SERIAL_H

#include <kernel/types.h>

void serial_init(void);
void serial_putc(char c);
void serial_puts(const char *str);

#endif
