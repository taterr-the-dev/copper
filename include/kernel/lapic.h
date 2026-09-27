#ifndef _KERNEL_LAPIC_H
#define _KERNEL_LAPIC_H
void lapic_init(void);
void lapic_timer_init(void);
void lapic_send_eoi(void);
#endif
