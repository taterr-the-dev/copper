#include <autoconf.h>
#ifdef CONFIG_USERMODE
#include <kernel/console.h>
#include <kernel/types.h>

static uint8_t ustack[16384] __attribute__((aligned(16)));

static void user_demo(void) {
  const char *m = "hello from ring 3 (Linux ABI)!\n";
  __asm__ volatile("syscall" ::"a"(1), "D"(1), "S"(m), "d"((uint64_t)31)
                   : "rcx", "r11", "memory");
  const char *m2 = "ring3 getpid -> ";
  __asm__ volatile("syscall" ::"a"(1), "D"(1), "S"(m2), "d"((uint64_t)16)
                   : "rcx", "r11", "memory");
  uint64_t pid;
  __asm__ volatile("syscall" : "=a"(pid) : "a"(39) : "rcx", "r11");
  for (;;)
    __asm__ volatile("pause");
}

void enter_usermode(uint64_t entry, uint64_t sp) {
  __asm__ volatile("cli\n"
                   "mov %0, %%rdi\n"
                   "mov %1, %%rsi\n"
                   "mov $0x23, %%rax\n"
                   "push %%rax\n"
                   "push %%rsi\n"
                   "push $0x202\n"
                   "mov $0x2b, %%rax\n"
                   "push %%rax\n"
                   "push %%rdi\n"
                   "iretq\n"
                   :
                   : "r"(entry), "r"(sp)
                   : "rax", "rdi", "rsi", "memory", "cc");
  for (;;)
    ;
}

void user_thread(void *a) {
  (void)a;
  con_puts("[USER] dropping to ring 3...\n");
  enter_usermode((uint64_t)user_demo, (uint64_t)(ustack + sizeof(ustack)));
}
#endif
