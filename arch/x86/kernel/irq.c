#include <autoconf.h>
#include <kernel/console.h>
#include <kernel/interrupts.h>
#include <kernel/panic.h>
#include <kernel/pmm.h>
#include <kernel/string.h>
#include <kernel/timer.h>
#ifdef CONFIG_SCHED
#include <kernel/proc.h>
#include <kernel/vm.h>
#endif
#include <kernel/acpi.h>

extern void timer_tick(void);
extern void keyboard_irq(void);
extern uint64_t debug_syscall_ret;
extern void lapic_send_eoi(void);

void hex64(uint64_t v) {
  for (int i = 60; i >= 0; i -= 4) {
    int d = (v >> i) & 15;
    con_putc(d < 10 ? '0' + d : 'a' + d - 10);
  }
}
extern void put_u64(uint64_t v);

typedef void (*irq_handler_t)(void);
static irq_handler_t irq_handlers[16] = {0};

void irq_register(int irq, irq_handler_t handler) {
  if (irq >= 0 && irq < 16) {
    irq_handlers[irq] = handler;
  }
}

void isr_handler(struct int_frame *f) {
  if (f->int_no < 32) {
#ifdef CONFIG_SCHED
    if (f->int_no == 14 && current) {
      uint64_t cr2;
      __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
      if ((f->err & 3) == 3) {
        if (vm_cow_handle((uint64_t *)current->cr3, cr2)) {
          return;
        }
      }

      if (!(f->err & 1) && cr2 >= 0x10000 && cr2 < 0x0000800000000000ULL) {
        if (cr2 >= 0x0000700000200000ULL && cr2 < 0x0000700000300000ULL) {
          if ((f->cs & 3) == 3) {
            kill_current(f->int_no);
            return;
          }
        }

        extern int handle_file_page_fault(uint64_t addr, uint64_t err_code);
        if (handle_file_page_fault(cr2, f->err)) {
          return;
        }

        uint64_t pg = pmm_alloc();
        if (pg) {
          vm_map((uint64_t *)current->cr3, cr2 & ~0xFFFULL, pg, 0x07);
          memset((void *)pg, 0, 4096);
          __asm__ volatile("invlpg (%0)" ::"r"(cr2) : "memory");
          return;
        }
      }
      if ((f->cs & 3) == 3) {
        con_puts("[KILL] PID ");
        hex64(current->pid);
        con_puts(" vec=");
        hex64(f->int_no);
        con_puts(" rip=");
        hex64(f->rip);
        con_puts(" cr2=");
        hex64(cr2);
        con_puts(" err=");
        hex64(f->err);
        con_puts("\n  rax=");
        hex64(f->rax);
        con_puts(" rbx=");
        hex64(f->rbx);
        con_puts(" rcx=");
        hex64(f->rcx);
        con_puts(" rdx=");
        hex64(f->rdx);
        con_puts("\n  rsi=");
        hex64(f->rsi);
        con_puts(" rdi=");
        hex64(f->rdi);
        con_puts(" rbp=");
        hex64(f->rbp);
        con_puts(" rsp=");
        hex64(f->rsp);
        con_puts("\n");

        kill_current(f->int_no);
        return;
      }
    }

    if ((f->cs & 3) == 3) {
      con_puts("[KILL] User exception #");
      put_u64(f->int_no);
      con_puts(" at RIP=");
      hex64(f->rip);
      con_puts("\n");
      kill_current(f->int_no);
      return;
    }
#endif
    panic_frame(f);
  }

  uint8_t irq = f->int_no - 32;

  if (irq == 0) {
    timer_tick();
#ifdef CONFIG_SCHED
    sched_tick();
#endif
#ifdef CONFIG_LAPIC
    lapic_send_eoi();
#else
    timer_send_eoi();
#endif
    return;
  }
  if (irq < 16) {
    if (irq_handlers[irq]) {
      irq_handlers[irq]();
    } else if (irq == 1) {
      keyboard_irq();
    }
    if (acpi.initialized && irq == acpi.sci_interrupt) {
        acpi_sci_handler();
    }
    pic_sendEOI(irq);
#ifdef CONFIG_LAPIC
    lapic_send_eoi();
#endif
  }

#ifdef CONFIG_SCHED
  if ((f->cs & 3) == 3 && current) {
    deliver_signals(f);
  }
#endif
}
