#ifndef _KERNEL_PANIC_H
#define _KERNEL_PANIC_H
#include <kernel/types.h>
struct int_frame;
void panic(const char *msg);
void panic_frame(struct int_frame *f);
#endif
