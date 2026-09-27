#include <autoconf.h>
#include <kernel/timer.h>
#include <kernel/console.h>

#ifdef CONFIG_LAPIC
    #include <kernel/lapic.h>

    void timer_init(void) {
        lapic_init();
        lapic_timer_init();
    }

    void timer_send_eoi(void) {
        lapic_send_eoi();
    }
#else
    #include <kernel/pit.h>
    #include <kernel/interrupts.h>

    void timer_init(void) {
        pit_init(100);
        con_puts("[PIT] Legacy timer initialized at 100Hz.\n");
    }

    void timer_send_eoi(void) {
        extern void pic_sendEOI(uint8_t irq);
        pic_sendEOI(0);
    }
#endif
