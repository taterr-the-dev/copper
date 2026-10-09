#include <autoconf.h>
#include <kernel/blkdev.h>
#include <kernel/console.h>
#include <kernel/gdt.h>
#include <kernel/idt.h>
#include <kernel/panic.h>
#include <kernel/types.h>
#ifdef CONFIG_INTERRUPTS
#include <kernel/interrupts.h>
#ifdef CONFIG_SCHED
#include <kernel/proc.h>
#endif
#ifdef CONFIG_USERMODE
extern void user_thread(void *);
#endif
#ifdef CONFIG_EXECVE
#include <kernel/proc.h>
#endif
#endif
#ifdef CONFIG_ELF_LOADER
#include <kernel/elf.h>
#endif
#ifdef CONFIG_FS_VFS
#include <kernel/fs.h>
#include <kernel/multiboot.h>
#include <kernel/string.h>
#ifdef CONFIG_KMALLOC
#include <kernel/kmalloc.h>
#endif
#ifdef CONFIG_VM
#include <kernel/pmm.h>
#include <kernel/vm.h>
#endif
#ifdef CONFIG_BINFMT_SHEBANG
#include <kernel/binfmt.h>
#endif
#endif
#ifdef CONFIG_SYSCALLS
#include <kernel/syscall.h>
#endif
#include <kernel/timer.h>
#include <kernel/arch.h>
#include <kernel/acpi.h>
#include <kernel/acpi_bus.h>
#include <kernel/pci.h>
#include <kernel/ioapic.h>

extern void put_u64(uint64_t v);
#ifdef CONFIG_RUN_INIT
static void init_thread(void *arg) {
  (void)arg;
  int rc = execve(CONFIG_INIT_PATH, 0, 0);
  for (;;)
    arch_hlt();
}
#endif

#ifdef CONFIG_PCI_RAMDISK
extern int pci_ramdisk_init(void);
#endif

void kernel_main(uint32_t magic, uint32_t mboot_ptr) {
  con_init();
  con_puts("\n=====================================\n");
  con_puts("  Copper Kernel v0.2-rc3\n");
  con_puts("=====================================\n");

  if (magic == 0x2BADB002)
    con_puts("[OK] Booted via Multiboot.\n");
  else
    con_puts("[!!] Invalid Multiboot magic!\n");

  gdt_init();
  arch_init_fpu();
  con_puts("[OK] GDT initialized.\n");
  idt_init();
#ifdef CONFIG_KMALLOC
  kmalloc_init();
#ifdef CONFIG_VM
  vm_init();
  pmm_init();
  mb_init((uint64_t)(uint32_t)mboot_ptr);
  {
    extern char _kernel_end[];
    uint64_t kend = ((uint64_t)_kernel_end + 0x100000) & ~0xFFFULL;
    pmm_reserve(0x100000, kend);
    con_puts("[OK] pmm: kernel reserved up to ");
    {
      const char *hx = "0123456789abcdef";
      for (int sh = 28; sh >= 0; sh -= 4)
        con_putc(hx[(kend >> sh) & 15]);
    }
    con_puts("\n");
  }
  con_puts("[OK] VM: per-process address spaces on.\n");
  vm_sanitize_boot_cr3();
#endif
#endif
#ifdef CONFIG_FS_VFS
  fs_init();
  ata_init();
  for (int i = 0; i < blkdev_count(); i++)
    blkdev_scan_partitions(blkdev_get(i));
  if (blkdev_count()) {
    int mi = -1;
    for (int i = 0; i < blkdev_count(); i++) {
      if (vfs_mount(blkdev_get(i)) >= 0) {
        mi = i;
        break;
      }
    }
    if (mi >= 0) {
      con_puts("[FS] mounted ");
      con_puts(vfs_name());
      con_puts(" on ");
      con_puts(blkdev_get(mi)->name);
      con_puts("\n");
    } else
      con_puts("[FS] no recognizable filesystem\n");
  }
  {
    extern struct fs_ops proc_fs;
    void *proc_sb = NULL;
    if (proc_fs.mount(NULL, &proc_sb) == 0) {
      vfs_mount_at("/proc", &proc_fs, proc_sb);
    }
  }
  {
    extern struct fs_ops tmp_fs;
    void *tmp_sb = NULL;
    if (tmp_fs.mount(NULL, &tmp_sb) == 0) {
      vfs_mount_at("/tmp", &tmp_fs, tmp_sb);
    }
  }
  setup_std_fds(1);
#endif

#ifdef CONFIG_ELF_LOADER
  con_puts("[OK] ELF loader compiled in.\n");
  elf_selftest();
#endif

#ifdef CONFIG_SYSCALLS
  syscall_init();
  con_puts("[OK] Syscall interface initialized.\n");
  syscall_selftest();
#endif

#ifdef CONFIG_INTERRUPTS
  pic_remap(32, 40);
  idt_install();
  timer_init();
#ifdef CONFIG_PS2_KEYBOARD
  kbd_init();
#endif
#ifdef CONFIG_LAPIC
  pic_set_mask(0);
#else
  pic_clear_mask(0);
#endif
  pic_clear_mask(1);
  pic_clear_mask(2);
  pic_clear_mask(3);
  pic_clear_mask(4);
  pic_set_mask(5);
  pic_set_mask(6);
  pic_set_mask(7);
  pic_set_mask(8);
  pic_set_mask(9);
  pic_set_mask(10);
  pic_set_mask(11);
  pic_set_mask(12);
  pic_set_mask(13);
  pic_clear_mask(14);
  pic_clear_mask(15);
  con_puts("[OK] Interrupts enabled.\n");
  arch_sti();
#endif

#ifdef CONFIG_KMALLOC
  {
    void *a = kmalloc(100), *b = kmalloc(50);
    kfree(a);
    void *c = kmalloc(80);
    con_puts("[OK] kmalloc: a=");
    (void)a;
    (void)b;
    (void)c;
    con_puts("\n");
#ifdef CONFIG_FS_TMPFS
    con_puts("[TMPFS] ");
    vfs_create("/tmp/hello");
    struct fs_file tf;
    if (vfs_open("/tmp/hello", &tf) == 0) {
      const char *m = "tmpfs works";
      vfs_write(&tf, m, 11);
      vfs_close(&tf);
    }
    struct fs_file rf;
    if (vfs_open("/tmp/hello", &rf) == 0) {
      char b[32] = {0};
      vfs_read(&rf, b, 31);
      vfs_close(&rf);
      con_puts(strcmp(b, "tmpfs works") ? "write/read=FAIL "
                                        : "write/read=OK ");
    }
    struct fs_dirent td;
    if (vfs_readdir("/tmp", 0, &td) == 0 && !strcmp(td.name, "hello"))
      con_puts("readdir=OK ");
    else
      con_puts("readdir=FAIL ");
    if (vfs_unlink("/tmp/hello") == 0 && vfs_open("/tmp/hello", &rf) != 0)
      con_puts("unlink=OK\n");
    else
      con_puts("unlink=FAIL\n");
#endif
    con_puts("[PROCFS] ");
    struct fs_file pf;
    if (vfs_open("/proc/uptime", &pf) == 0) {
      char b[64] = {0};
      vfs_read(&pf, b, 63);
      vfs_close(&pf);
      con_puts("uptime=");
      con_puts(b);
    }
    if (vfs_open("/proc/tasks", &pf) == 0) {
      char b[128] = {0};
      vfs_read(&pf, b, 127);
      vfs_close(&pf);
      con_puts(" tasks=");
      con_puts(b);
    }
    con_puts("\n");
    con_puts(a && b && c ? "ok\n" : "FAIL\n");
  }
#endif
#ifdef CONFIG_BINFMT_SHEBANG
  binfmt_init();
#endif

#ifdef CONFIG_PANIC_SELFTEST
  panic("selftest panic");
#endif
#ifdef CONFIG_NET_RTL8139
  extern void rtl8139_init();
  rtl8139_init();
#endif
#ifdef CONFIG_NET_E1000
  extern void e1000_init();
  e1000_init();
#endif
#ifdef CONFIG_NET
  extern void net_init();
  net_init();
#endif
  extern void bcache_init(void);
  bcache_init();
#ifdef CONFIG_PCI_RAMDISK
  if (pci_ramdisk_init() == 0) {
    con_puts("[PCI-RAMDISK] PCI RAM disk ready\n");
  }
#endif
	acpi_init();
	acpi_bus_init();
  if (acpi.ioapic_count > 0) {
    ioapic_init(acpi.ioapic_addresses[0]);
    for (int i = 0; i < 16; i++) {
      ioapic_route_irq(i, 32 + i, 0);
      pic_set_mask(i);
    }
  }
	pci_init();
#ifdef CONFIG_SCHED
  scheduler_init();
  extern void demo_a(void *), demo_b(void *);
  con_puts("[OK] Scheduler online (threads A/B).\n");
#endif
  con_puts("\n[SUCCESS] Copper Kernel core initialized!\n");
#ifdef CONFIG_SCHED
#ifdef CONFIG_RUN_INIT
  con_puts("[RUNNING] Running init.\n");
  kthread_create(init_thread, 0, "init");
#else
  con_puts("[IDLE] idle loop (threads keep running).\n");
#endif
#endif
  for (;;)
    arch_hlt();
}

#ifdef CONFIG_SCHED
static void spin(void) {
  for (volatile int i = 0; i < 3000000; i++)
    ;
}
void demo_a(void *a) {
  (void)a;
  for (;;) {
    con_puts("A ");
    spin();
    yield();
  }
}
void demo_b(void *a) {
  (void)a;
  for (;;) {
    con_puts("B ");
    spin();
    yield();
  }
}
#endif
