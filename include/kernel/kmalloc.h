#ifndef _KERNEL_KMALLOC_H
#define _KERNEL_KMALLOC_H
#include <kernel/types.h>
void kmalloc_init(void);
void *kmalloc(size_t n);
void kfree(void *p);
#endif
