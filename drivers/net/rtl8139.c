#include <autoconf.h>
#include <kernel/console.h>
#include <kernel/interrupts.h>
#include <kernel/io.h>
#include <kernel/string.h>
#include <kernel/types.h>

#ifdef CONFIG_NET_RTL8139
extern void netif_rx_packet(uint8_t *pkt, int len);
extern uint64_t pmm_alloc(void);

static uint16_t io_base = 0;
uint8_t mac_addr[6];
static uint8_t *rx_buffer = NULL;
static uint8_t *tx_buffer = NULL;
static uint32_t rx_phys_addr = 0;
static uint32_t tx_phys_addr = 0;
static uint32_t rx_read_ptr = 0;

static uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t func,
                                  uint8_t offset) {
  uint32_t address = (uint32_t)((1 << 31) | (bus << 16) | (slot << 11) |
                                (func << 8) | (offset & 0xFC));
  outl(0xCF8, address);
  return inl(0xCFC);
}

static void rtl8139_irq() {
  if (!io_base)
    return;
  uint16_t status = inw(io_base + 0x3E);
  if (!status || status == 0xFFFF)
    return;
  outw(io_base + 0x3E, status);

  if (status & 0x01) {
    uint8_t *ring = rx_buffer;
    uint16_t cur_rx = inw(io_base + 0x38);
    while (rx_read_ptr != cur_rx) {
      uint32_t offset = rx_read_ptr % 8192;
      uint16_t rx_status = *(uint16_t *)(ring + offset);
      uint16_t rx_len = *(uint16_t *)(ring + offset + 2);
      if (!(rx_status & 0x01))
        break;
      netif_rx_packet(ring + offset + 4, rx_len - 4);
      rx_read_ptr += rx_len + 4;
      rx_read_ptr = (rx_read_ptr + 3) & ~3;
      outw(io_base + 0x38, rx_read_ptr - 16);
    }
  }
}

void rtl8139_init() {
  uint64_t rx_page = pmm_alloc();
  uint64_t tx_page = pmm_alloc();
  rx_buffer = (uint8_t *)rx_page;
  tx_buffer = (uint8_t *)tx_page;
  rx_phys_addr = (uint32_t)rx_page;
  tx_phys_addr = (uint32_t)tx_page;

  for (int bus = 0; bus < 256; bus++) {
    for (int slot = 0; slot < 32; slot++) {
      if (pci_config_read32(bus, slot, 0, 0) == 0x813910EC) {
        io_base = pci_config_read32(bus, slot, 0, 0x10) & 0xFFFC;
        uint16_t cmd = pci_config_read32(bus, slot, 0, 4) & 0xFFFF;
        cmd |= 0x04;
        outl(0xCF8, (1 << 31) | (bus << 16) | (slot << 11) | 4);
        outw(0xCFC, cmd);

        for (int i = 0; i < 6; i++)
          mac_addr[i] = inb(io_base + i);
        outb(io_base + 0x52, 0x00);
        outb(io_base + 0x37, 0x10);
        while ((inb(io_base + 0x37) & 0x10) != 0) {
        }

        outl(io_base + 0x30, rx_phys_addr);
        outl(io_base + 0x44, 0x0000001F);
        outl(io_base + 0x10, 0);
        outl(io_base + 0x14, 0);
        outl(io_base + 0x18, 0);
        outl(io_base + 0x1C, 0);
        outb(io_base + 0x37, 0x0C);
        outw(io_base + 0x3C, 0x0005);

        uint8_t irq_line = pci_config_read32(bus, slot, 0, 0x3C) & 0xFF;
        irq_register(irq_line, rtl8139_irq);
        extern void pic_clear_mask(uint8_t irq);
        pic_clear_mask(irq_line);
        pic_clear_mask(2);

        con_puts("[RTL8139] Initialized and online!\n");
        return;
      }
    }
  }
}

void rtl8139_tx(uint8_t *pkt, int len) {
  if (!io_base)
    return;
  int timeout = 1000000;
  while ((inl(io_base + 0x10) & 0x2000) && timeout > 0)
    timeout--;
  if (timeout == 0)
    outl(io_base + 0x10, 0);
  memcpy(tx_buffer, pkt, len);
  outl(io_base + 0x20, tx_phys_addr);
  outl(io_base + 0x10, len);
}

int rtl8139_is_active() { return io_base != 0; }
#endif
