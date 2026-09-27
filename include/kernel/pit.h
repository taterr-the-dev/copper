#ifndef _KERNEL_PIT_H
#define _KERNEL_PIT_H

#include <kernel/types.h>

void pit_init(uint32_t hz);
void timer_tick(void);

#endif
