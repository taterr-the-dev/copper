SFILES += arch/x86/boot.s \
          arch/x86/isr.s \
          arch/x86/switch.s \
          arch/x86/syscall.s

CFILES += arch/x86/kernel/syscall.c \
          arch/x86/kernel/vm.c \
          arch/x86/kernel/gdt.c \
          arch/x86/kernel/idt.c \
          arch/x86/kernel/irq.c \
          arch/x86/kernel/timer.c \
          arch/x86/kernel/pic.c \
          arch/x86/kernel/pit.c \
          arch/x86/kernel/pmm.c \
          arch/x86/kernel/proc.c \
          arch/x86/kernel/multiboot.c \
          arch/x86/kernel/exec.c \
          arch/x86/kernel/user.c \
          arch/x86/kernel/acpi_arch.c \
          arch/x86/kernel/pci.c \
          arch/x86/kernel/ioapic.c \
          arch/x86/kernel/arch.c

ifeq ($(CONFIG_LAPIC),y)
    CFILES += arch/x86/kernel/lapic.c
endif
