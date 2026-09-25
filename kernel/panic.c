#include <autoconf.h>
#include <kernel/console.h>
#include <kernel/interrupts.h>
#include <kernel/io.h>
#include <kernel/panic.h>
static const char *hx = "0123456789abcdef";
static void p2(uint8_t v) {
  con_putc(hx[v >> 4]);
  con_putc(hx[v & 15]);
}
static void ph(uint64_t v) {
  for (int i = 60; i >= 0; i -= 4)
    con_putc(hx[(v >> i) & 15]);
}
static const char *exc_name(int v) {
  switch (v) {
  case 0:
    return "divide error";
  case 6:
    return "invalid opcode";
  case 8:
    return "double fault";
  case 13:
    return "general protection";
  case 14:
    return "page fault";
  case 10:
    return "invalid TSS";
  case 11:
    return "segment not present";
  case 12:
    return "stack segment";
  case 16:
    return "x87 FPU";
  case 19:
    return "SIMD";
  default:
    return "exception";
  }
}
static void halt_or_reboot(void) {
#ifdef CONFIG_PANIC_REBOOT
  outb(0x64, 0xFE);
#endif
  __asm__ volatile("cli");
  for (;;)
    __asm__ volatile("hlt");
}
void panic(const char *msg) {
  __asm__ volatile("cli");
  con_puts("\n\n");
  con_puts("========================================\n");
  con_puts("***          KERNEL PANIC          ***\n");
  con_puts("========================================\n");
  con_puts(msg);
  con_puts("\n");
  halt_or_reboot();
}
void panic_frame(struct int_frame *f) {
  __asm__ volatile("cli");
  con_puts("\n\n");
  con_puts("========================================\n");
  con_puts("*** KERNEL PANIC: ");
  con_puts(exc_name((int)f->int_no));
  con_puts(" (");
  p2((uint8_t)f->int_no);
  con_puts(") ***\n");
  con_puts("========================================\n");
  con_puts("err=");
  ph(f->err);
  con_puts("\n");
  con_puts("RIP=");
  ph(f->rip);
  con_puts("  CS=");
  ph(f->cs);
  con_puts("\n");
  con_puts("RSP=");
  ph(f->rsp);
  con_puts("  RFLAGS=");
  ph(f->rflags);
  con_puts("\n");
  con_puts("RAX=");
  ph(f->rax);
  con_puts(" RBX=");
  ph(f->rbx);
  con_puts(" RCX=");
  ph(f->rcx);
  con_puts("\n");
  con_puts("RDX=");
  ph(f->rdx);
  con_puts(" RSI=");
  ph(f->rsi);
  con_puts(" RDI=");
  ph(f->rdi);
  con_puts("\n");
  uint64_t cr2, cr3;
  __asm__ volatile("mov %%cr2,%0" : "=r"(cr2));
  __asm__ volatile("mov %%cr3,%0" : "=r"(cr3));
  con_puts("CR2=");
  ph(cr2);
  con_puts("  CR3=");
  ph(cr3);
  con_puts("\n");
  con_puts("stack:");
  for (int i = 0; i < 8; i++) {
    con_puts(" ");
    ph(((uint64_t *)f->rsp)[i]);
  }
  con_puts("\n");
  halt_or_reboot();
}
