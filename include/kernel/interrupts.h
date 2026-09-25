#ifndef _KERNEL_INTERRUPTS_H
#define _KERNEL_INTERRUPTS_H
#include <kernel/types.h>
struct int_frame {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t int_no, err;
    uint64_t rip, cs, rflags, rsp, ss;
};
void pic_remap(uint32_t o1, uint32_t o2);
void pic_set_mask(uint8_t irq);
void pic_clear_mask(uint8_t irq);
void pic_sendEOI(uint8_t irq);
void idt_install(void);
void pit_init(uint32_t hz);
void kbd_init(void);
char kbd_getchar(void);
void irq_register(int irq, void (*handler)(void));
extern volatile uint64_t timer_ticks;
#endif
