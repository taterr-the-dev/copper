CFILES += drivers/acpi_cpu.c
ifeq ($(CONFIG_SERIAL_UART),y)
    CFILES += drivers/serial.c
endif
ifeq ($(CONFIG_VGA_TEXT),y)
    CFILES += drivers/vga.c
endif
ifeq ($(CONFIG_PS2_KEYBOARD),y)
    CFILES += drivers/keyboard.c
endif
ifeq ($(CONFIG_ATA),y)
    CFILES += drivers/ata.c
endif
ifeq ($(CONFIG_RAMDISK),y)
    CFILES += drivers/ramdisk.c
endif
ifeq ($(CONFIG_PCI_RAMDISK),y)
    CFILES += drivers/pci_ramdisk.c
endif
ifeq ($(CONFIG_NET_RTL8139),y)
    CFILES += drivers/net/rtl8139.c
endif
ifeq ($(CONFIG_NET_E1000),y)
	CFILES += drivers/net/e1000.c
endif
