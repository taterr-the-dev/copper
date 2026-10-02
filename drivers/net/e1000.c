#include <autoconf.h>
#include <kernel/console.h>
#include <kernel/interrupts.h>
#include <kernel/io.h>
#include <kernel/pmm.h>
#include <kernel/string.h>
#include <kernel/types.h>
#include <kernel/vm.h>
#include <kernel/arch.h>

#ifdef CONFIG_NET_E1000

extern void netif_rx_packet(uint8_t *pkt, int len);
extern void put_u64(uint64_t v);

static volatile void *mmio_base = NULL;
uint8_t e1000_mac[6];
static int e1000_active = 0;

struct e1000_rx_desc {
  volatile uint64_t addr;
  volatile uint16_t length;
  volatile uint16_t checksum;
  volatile uint8_t status;
  volatile uint8_t errors;
  volatile uint16_t special;
} __attribute__((packed));

struct e1000_tx_desc {
  volatile uint64_t addr;
  volatile uint16_t length;
  volatile uint8_t cso;
  volatile uint8_t cmd;
  volatile uint8_t status;
  volatile uint8_t css;
  volatile uint16_t special;
} __attribute__((packed));

#define NUM_RX_DESC 64
#define NUM_TX_DESC 64

static struct e1000_rx_desc *rx_ring;
static struct e1000_tx_desc *tx_ring;
static uint8_t *rx_buffers[NUM_RX_DESC];
static uint8_t *tx_buffers[NUM_TX_DESC];
static uint32_t rx_ring_phys;
static uint32_t tx_ring_phys;

static uint16_t tx_idx = 0;
static uint16_t rx_idx = 0;

static uint32_t e1000_reg_read(uint32_t reg) {
  return *(volatile uint32_t *)(mmio_base + reg);
}
static void e1000_reg_write(uint32_t reg, uint32_t val) {
  *(volatile uint32_t *)(mmio_base + reg) = val;
}

static uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t func,
                                  uint8_t offset) {
  uint32_t address = (uint32_t)((1 << 31) | (bus << 16) | (slot << 11) |
                                (func << 8) | (offset & 0xFC));
  outl(0xCF8, address);
  return inl(0xCFC);
}

static void e1000_irq() {
  uint32_t status = e1000_reg_read(0x00C0);
  if (!status)
    return;

  if (status & (0x80000 | 0x80)) {
    while (rx_ring[rx_idx].status & 0x01) {
      uint16_t len = rx_ring[rx_idx].length;
      if (len == 0 || len > 4096) {
        rx_ring[rx_idx].status = 0;
        rx_idx = (rx_idx + 1) % NUM_RX_DESC;
        continue;
      }
      if (len > 2000) {
        rx_ring[rx_idx].status = 0;
        rx_idx = (rx_idx + 1) % NUM_RX_DESC;
        continue;
      }

      netif_rx_packet(rx_buffers[rx_idx], len);
      rx_ring[rx_idx].status = 0;
      rx_idx = (rx_idx + 1) % NUM_RX_DESC;
    }

    uint16_t last_cleaned = (rx_idx == 0) ? NUM_RX_DESC - 1 : rx_idx - 1;
    e1000_reg_write(0x02818, last_cleaned);
  }
}

void e1000_init() {
  for (int bus = 0; bus < 256; bus++) {
    for (int slot = 0; slot < 32; slot++) {
      if (pci_config_read32(bus, slot, 0, 0) == 0x100E8086) {
        uint32_t mmio_phys = pci_config_read32(bus, slot, 0, 0x10) & ~0xF;
        uint64_t cr3 = arch_read_cr3();
        for (int p = 0; p < 32; p++) {
          vm_map((uint64_t *)cr3, mmio_phys + p * 0x1000,
                 mmio_phys + p * 0x1000, 0x03);
        }
        mmio_base = (volatile void *)(uint64_t)mmio_phys;

        uint16_t cmd = pci_config_read32(bus, slot, 0, 4) & 0xFFFF;
        cmd |= 0x06;
        outl(0xCF8, (1 << 31) | (bus << 16) | (slot << 11) | 4);
        outw(0xCFC, cmd);

        e1000_reg_write(0x0000, 0x04000000);
        for (volatile int i = 0; i < 100000; i++)
          ;

        while (e1000_reg_read(0x0000) & 0x04000000) {
          for (volatile int i = 0; i < 1000; i++)
            ;
        }

        for (int i = 0; i < 3; i++) {
          e1000_reg_write(0x0014, (1 << 0) | (i << 8));
          while (!(e1000_reg_read(0x0014) & (1 << 4)))
            ;
          uint32_t val = e1000_reg_read(0x0014) >> 16;
          e1000_mac[i * 2] = val & 0xFF;
          e1000_mac[i * 2 + 1] = (val >> 8) & 0xFF;
        }

        uint32_t rar_low = (e1000_mac[0]) | (e1000_mac[1] << 8) |
                           (e1000_mac[2] << 16) | (e1000_mac[3] << 24);
        uint32_t rar_high = (e1000_mac[4]) | (e1000_mac[5] << 8) | (1 << 31);
        e1000_reg_write(0x05400, rar_low);
        e1000_reg_write(0x05404, rar_high);

        uint64_t rx_p = pmm_alloc();
        vm_map((uint64_t *)cr3, rx_p, rx_p, 0x1B);
        rx_ring = (struct e1000_rx_desc *)rx_p;
        rx_ring_phys = (uint32_t)rx_p;

        uint64_t tx_p = pmm_alloc();
        vm_map((uint64_t *)cr3, tx_p, tx_p, 0x1B);
        tx_ring = (struct e1000_tx_desc *)tx_p;
        tx_ring_phys = (uint32_t)tx_p;

        for (int i = 0; i < NUM_RX_DESC; i++) {
          uint64_t buf_p = pmm_alloc();
          vm_map((uint64_t *)cr3, buf_p, buf_p, 0x1B);
          rx_buffers[i] = (uint8_t *)buf_p;
          rx_ring[i].addr = buf_p;
          rx_ring[i].status = 0;
        }

        for (int i = 0; i < NUM_TX_DESC; i++) {
          uint64_t buf_p = pmm_alloc();
          vm_map((uint64_t *)cr3, buf_p, buf_p, 0x1B);
          tx_buffers[i] = (uint8_t *)buf_p;
          tx_ring[i].addr = buf_p;
          tx_ring[i].status = 0x01;
        }

        e1000_reg_write(0x02800, rx_ring_phys);
        e1000_reg_write(0x02804, 0);
        e1000_reg_write(0x02808, NUM_RX_DESC * 16);
        e1000_reg_write(0x02810, 0);
        e1000_reg_write(0x02818, NUM_RX_DESC - 1);
        e1000_reg_write(0x00100, 0x04038002);

        e1000_reg_write(0x03800, tx_ring_phys);
        e1000_reg_write(0x03804, 0);
        e1000_reg_write(0x03808, NUM_TX_DESC * 16);
        e1000_reg_write(0x03810, 0);
        e1000_reg_write(0x03818, 0);

        e1000_reg_write(0x00D0, 0x800C0);
        e1000_reg_read(0x00C0);

        uint8_t irq_line = pci_config_read32(bus, slot, 0, 0x3C) & 0xFF;
        irq_register(irq_line, e1000_irq);
        extern void pic_clear_mask(uint8_t irq);
        pic_clear_mask(irq_line);
        pic_clear_mask(2);

        uint32_t ctrl = e1000_reg_read(0x0000);
        ctrl |= 0x00000241;
        e1000_reg_write(0x0000, ctrl);

        for (int i = 0; i < 1000000; i++) {
          uint32_t status = e1000_reg_read(0x0008);
          if (status & 0x02) {
            break;
          }
          if (i == 999999) {
            con_puts("[E1000] WARNING: Link timeout\n");
          }
        }

        e1000_reg_write(0x00400, 0x000400FB);

        e1000_active = 1;
        con_puts("[E1000] Initialized and online!\n");
        return;
      }
    }
  }
}

void e1000_tx(uint8_t *pkt, int len) {
  if (!e1000_active)
    return;

  uint64_t cr3 = arch_read_cr3();
  uint32_t mmio_phys = (uint32_t)(uint64_t)mmio_base;
  for (int p = 0; p < 32; p++) {
    extern void vm_map(uint64_t *cr3, uint64_t virt, uint64_t phys,
                       uint64_t flags);
    vm_map((uint64_t *)cr3, mmio_phys + p * 0x1000, mmio_phys + p * 0x1000,
           0x03);
  }

  e1000_reg_write(0x0000, 0x00001A41);
  e1000_reg_write(0x00400, 0x000400FB);

  uint32_t status = 0;
  for (int i = 0; i < 100000; i++) {
    status = e1000_reg_read(0x0008);
    if (status & 0x02) {
      break;
    }
  }

  while (!(tx_ring[tx_idx].status & 0x01)) {
  }

  memcpy(tx_buffers[tx_idx], pkt, len);
  tx_ring[tx_idx].length = len;
  tx_ring[tx_idx].cmd = 0x0B;
  tx_ring[tx_idx].status = 0;

  e1000_reg_write(0x03818, (tx_idx + 1) % NUM_TX_DESC);
  tx_idx = (tx_idx + 1) % NUM_TX_DESC;
}

void e1000_poll_rx(void) {
  while (rx_ring[rx_idx].status & 0x01) {
    uint16_t len = rx_ring[rx_idx].length;
    if (len == 0 || len > 4096) {
      rx_ring[rx_idx].status = 0;
      rx_idx = (rx_idx + 1) % NUM_RX_DESC;
      continue;
    }
    if (len > 0 && len <= 2000) {
      netif_rx_packet(rx_buffers[rx_idx], len);
    }
    rx_ring[rx_idx].status = 0;
    rx_idx = (rx_idx + 1) % NUM_RX_DESC;
  }
  uint16_t last_cleaned = (rx_idx == 0) ? NUM_RX_DESC - 1 : rx_idx - 1;
  e1000_reg_write(0x02818, last_cleaned);
}

int e1000_is_active() { return e1000_active; }
#endif
