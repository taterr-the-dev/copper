#include <kernel/types.h>
#include <kernel/io.h>
#include <kernel/console.h>
#include <kernel/pci.h>
#include <kernel/tty.h>

extern void hex64(uint64_t v);

#define UHCI_USBCMD     0x00
#define UHCI_USBSTS     0x02
#define UHCI_FLBASEADD  0x08
#define UHCI_PORTSC1    0x10
#define UHCI_PORTSC2    0x12

#define USBCMD_RS       (1 << 0)
#define USBCMD_HCRESET  (1 << 1)
#define USBCMD_CF       (1 << 6)

#define PORTSC_CCS      (1 << 0)
#define PORTSC_CSC      (1 << 1)
#define PORTSC_PE       (1 << 2)
#define PORTSC_PR       (1 << 4)

static uint32_t __attribute__((aligned(4096))) uhci_frame_list[1024];

static void uhci_write16(uint16_t base, uint8_t offset, uint16_t val) { outw(base + offset, val); }
static uint16_t uhci_read16(uint16_t base, uint8_t offset) { return inw(base + offset); }
static void uhci_write32(uint16_t base, uint8_t offset, uint32_t val) { outl(base + offset, val); }

uint64_t virt_to_phys(uint64_t addr) {
  if (addr >= 0xFFFF800000000000ULL) {
    return addr - 0xFFFF800000000000ULL;
  }
  return addr;
}

struct uhci_td {
  volatile uint32_t link;
  volatile uint32_t ctrl;
  volatile uint32_t token;
  volatile uint32_t buffer;
} __attribute__((packed, aligned(16)));

struct uhci_qh {
  uint32_t link;
  uint32_t elem;
} __attribute__((packed, aligned(16)));

struct usb_setup_packet {
  uint8_t  bmRequestType;
  uint8_t  bRequest;
  uint16_t wValue;
  uint16_t wIndex;
  uint16_t wLength;
} __attribute__((packed));

static struct uhci_qh __attribute__((aligned(16))) uhci_qh;
static struct uhci_td __attribute__((aligned(16))) uhci_td_setup;
static struct uhci_td __attribute__((aligned(16))) uhci_td_data;
static struct uhci_td __attribute__((aligned(16))) uhci_td_status;
static struct usb_setup_packet __attribute__((aligned(16))) static_setup;
static uint8_t __attribute__((aligned(16))) static_desc_buf[32];

int uhci_control_transfer(uint16_t io_base, uint8_t addr, struct usb_setup_packet *setup, void *data_buf, uint16_t data_len, int is_low_speed) {
  (void)io_base;
  uint64_t p_setup = virt_to_phys((uint64_t)setup);
  uint64_t p_data = virt_to_phys((uint64_t)data_buf);
  uint64_t p_td_setup = virt_to_phys((uint64_t)&uhci_td_setup);
  uint64_t p_td_data = virt_to_phys((uint64_t)&uhci_td_data);
  uint64_t p_td_status = virt_to_phys((uint64_t)&uhci_td_status);
  uint64_t p_qh = virt_to_phys((uint64_t)&uhci_qh);
  uint32_t ctrl_setup = (1 << 23) | (3 << 27) | (is_low_speed ? (1 << 26) : 0);
  uint32_t token_setup = (0x2D << 0) | (addr << 8) | (0 << 15) | (0 << 19) | (7 << 21);
  uhci_td_setup.link = (p_td_data & 0xFFFFFFF0);
  uhci_td_setup.ctrl = ctrl_setup;
  uhci_td_setup.token = token_setup;
  uhci_td_setup.buffer = p_setup;
  uint8_t data_pid = data_len > 0 ? 0x69 : 0xE1;
  uint16_t max_len = data_len > 0 ? data_len - 1 : 0;
  uint32_t ctrl_data = (1 << 23) | (3 << 27) | (is_low_speed ? (1 << 26) : 0);
  uint32_t token_data = (data_pid << 0) | (addr << 8) | (0 << 15) | (1 << 19) | (max_len << 21);
  uhci_td_data.link = (p_td_status & 0xFFFFFFF0);
  uhci_td_data.ctrl = ctrl_data;
  uhci_td_data.token = token_data;
  uhci_td_data.buffer = p_data;
  uint8_t status_pid = data_len > 0 ? 0xE1 : 0x69;
  uint32_t ctrl_status = (1 << 23) | (3 << 27) | (is_low_speed ? (1 << 26) : 0);
  uint32_t token_status = (status_pid << 0) | (addr << 8) | (0 << 15) | (1 << 19) | (0 << 21);

  uhci_td_status.link = 0x00000001;
  uhci_td_status.ctrl = ctrl_status;
  uhci_td_status.token = token_status;
  uhci_td_status.buffer = 0;

  uhci_qh.link = 0x00000001;
  uhci_qh.elem = (p_td_setup & 0xFFFFFFF0);

  uint32_t qh_ptr = (p_qh & 0xFFFFFFF0) | 0x00000002;
  for (int i = 0; i < 1024; i++) {
    uhci_frame_list[i] = qh_ptr;
  }

  volatile int timeout = 10000000;
  while ((uhci_td_status.ctrl & (1 << 23)) && timeout > 0) {
    timeout--;
  }

  if (timeout == 0) {
    con_puts("[UHCI] Timeout! TD Ctrl States:\n");
    con_puts("  Setup: 0x"); hex64(uhci_td_setup.ctrl); con_puts("\n");
    con_puts("  Data:  0x"); hex64(uhci_td_data.ctrl); con_puts("\n");
    con_puts("  Status:0x"); hex64(uhci_td_status.ctrl); con_puts("\n");
    return -1;
  }

  if (uhci_td_setup.ctrl & 0x007E0000) {
    con_puts("[UHCI] Setup Error! Ctrl: 0x"); hex64(uhci_td_setup.ctrl); con_puts("\n");
    return -1;
  }

  return 0;
}

int uhci_set_address(uint16_t io_base, uint8_t new_addr, int is_low_speed) {
  static struct usb_setup_packet set_addr_setup;
  set_addr_setup.bmRequestType = 0x00;
  set_addr_setup.bRequest = 0x05;
  set_addr_setup.wValue = new_addr;
  set_addr_setup.wIndex = 0;
  set_addr_setup.wLength = 0;
  return uhci_control_transfer(io_base, 0x00, &set_addr_setup, NULL, 0, is_low_speed);
}

struct usb_config_descriptor {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t wTotalLength;
    uint8_t  bNumInterfaces;
    uint8_t  bConfigurationValue;
    uint8_t  iConfiguration;
    uint8_t  bmAttributes;
    uint8_t  bMaxPower;
} __attribute__((packed));

struct usb_interface_descriptor {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bInterfaceNumber;
    uint8_t  bAlternateSetting;
    uint8_t  bNumEndpoints;
    uint8_t  bInterfaceClass;
    uint8_t  bInterfaceSubClass;
    uint8_t  bInterfaceProtocol;
    uint8_t  iInterface;
} __attribute__((packed));

struct usb_endpoint_descriptor {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bEndpointAddress;
    uint8_t  bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t  bInterval;
} __attribute__((packed));

int usb_get_descriptor(uint16_t io_base, uint8_t addr, uint8_t type, uint8_t index, void *buf, uint16_t len, int is_low_speed) {
    static struct usb_setup_packet setup;
    setup.bmRequestType = 0x80;
    setup.bRequest = 0x06;
    setup.wValue = (type << 8) | index;
    setup.wIndex = 0;
    setup.wLength = len;
    return uhci_control_transfer(io_base, addr, &setup, buf, len, is_low_speed);
}

void uhci_init(struct pci_device *dev) {
  con_puts("[UHCI] Initializing USB Controller...\n");

  uint16_t cmd = pci_config_read16(dev->bus, dev->slot, dev->func, 0x04);
  cmd |= 0x05;
  pci_config_write16(dev->bus, dev->slot, dev->func, 0x04, cmd);

  uint16_t io_base = dev->bar4 & 0xFFFC;

  uint16_t usb_cmd = uhci_read16(io_base, UHCI_USBCMD);
  usb_cmd &= ~USBCMD_RS;
  uhci_write16(io_base, UHCI_USBCMD, usb_cmd);

  usb_cmd |= USBCMD_HCRESET;
  uhci_write16(io_base, UHCI_USBCMD, usb_cmd);

  int timeout = 100000;
  while ((uhci_read16(io_base, UHCI_USBCMD) & USBCMD_HCRESET) && timeout > 0) timeout--;
  if (timeout == 0) {
    con_puts("[UHCI] ERROR: Reset timed out!\n");
    return;
  }

  for (int i = 0; i < 1024; i++) uhci_frame_list[i] = 0x00000001;

  uint32_t frame_list_phys = virt_to_phys((uint64_t)uhci_frame_list);
  uhci_write32(io_base, UHCI_FLBASEADD, frame_list_phys);
  uhci_write16(io_base, UHCI_USBSTS, 0xFFFF);

  usb_cmd = uhci_read16(io_base, UHCI_USBCMD);
  usb_cmd |= USBCMD_RS | USBCMD_CF;
  uhci_write16(io_base, UHCI_USBCMD, usb_cmd);
  for (volatile int i = 0; i < 50000000; i++);

  con_puts("[UHCI] Controller ready.\n");

  uint8_t next_addr = 1;

  for (int port = 0; port < 2; port++) {
    uint8_t reg = (port == 0) ? UHCI_PORTSC1 : UHCI_PORTSC2;
    uint16_t status = uhci_read16(io_base, reg);
    
    if (!(status & PORTSC_CCS)) {
      continue;
    }

    int is_low_speed = (status & (1 << 7)) ? 1 : 0;
    uint8_t dev_addr = next_addr++;

    con_puts("[UHCI] Device detected on Port "); hex64(port + 1); con_puts("\n");

    status |= PORTSC_PR;
    uhci_write16(io_base, reg, status);
    for (volatile int i = 0; i < 1000000; i++);
    status = uhci_read16(io_base, reg);
    status &= ~PORTSC_PR;
    status |= PORTSC_CSC;
    uhci_write16(io_base, reg, status);
    for (volatile int i = 0; i < 5000000; i++);
    status = uhci_read16(io_base, reg);
    status |= PORTSC_PE;
    uhci_write16(io_base, reg, status);
    for (volatile int i = 0; i < 100000; i++);
    if (uhci_set_address(io_base, dev_addr, is_low_speed) != 0) {
      con_puts("[UHCI] SET_ADDRESS failed!\n");
      continue;
    }
    for (volatile int i = 0; i < 1000000; i++);
    static_setup.bmRequestType = 0x80;
    static_setup.bRequest = 0x06;
    static_setup.wValue = 0x0100;
    static_setup.wIndex = 0x0000;
    static_setup.wLength = 0x0012;

    if (uhci_control_transfer(io_base, dev_addr, &static_setup, static_desc_buf, 18, is_low_speed) == 0) {
      con_puts("[UHCI] Success! Vendor: 0x");
      hex64(static_desc_buf[8] | (static_desc_buf[9] << 8));
      con_puts(" Product: 0x");
      hex64(static_desc_buf[10] | (static_desc_buf[11] << 8));
      con_puts("\n");
    } else {
      con_puts("[UHCI] GET_DESCRIPTOR failed.\n");
    }
  }
}
