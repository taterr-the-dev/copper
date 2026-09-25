#include <autoconf.h>
#include <kernel/console.h>
#include <kernel/kmalloc.h>
#include <kernel/string.h>
#include <kernel/types.h>

#ifdef CONFIG_NET
extern void put_u64(uint64_t v);
uint32_t my_ip = 0x0F02000A;
static uint32_t my_mask = 0xFFFFFF00;
static uint32_t my_gw = 0xC0A80101;
uint8_t my_mac[6];

struct udp_packet {
  uint32_t src_ip;
  uint16_t src_port;
  uint16_t dst_port;
  int len;
  uint8_t data[1500];
  struct udp_packet *next;
};
static struct udp_packet *udp_queue_head = NULL;
static struct udp_packet *udp_queue_tail = NULL;

uint16_t ip_checksum(void *data, int len) {
  uint32_t sum = 0;
  uint8_t *bytes = (uint8_t *)data;
  for (int i = 0; i < len - 1; i += 2) {
    sum += (bytes[i] << 8) | bytes[i + 1];
  }
  if (len & 1) {
    sum += bytes[len - 1] << 8;
  }
  while (sum >> 16)
    sum = (sum & 0xFFFF) + (sum >> 16);
  return ~sum;
}

void netif_tx(uint8_t *pkt, int len) {
#ifdef CONFIG_NET_E1000
  extern int e1000_is_active(void);
  extern void e1000_tx(uint8_t *pkt, int len);
  if (e1000_is_active()) {
    e1000_tx(pkt, len);
    return;
  }
#endif
#ifdef CONFIG_NET_RTL8139
  extern void rtl8139_tx(uint8_t *pkt, int len);
  rtl8139_tx(pkt, len);
#endif
}

static void send_arp_reply(uint32_t target_ip, uint8_t *target_mac) {
  uint8_t pkt[42];
  memset(pkt, 0, 42);
  memcpy(pkt, target_mac, 6);
  memcpy(pkt + 6, my_mac, 6);
  pkt[12] = 0x08;
  pkt[13] = 0x06;
  pkt[14] = 0;
  pkt[15] = 1;
  pkt[16] = 0x08;
  pkt[17] = 0;
  pkt[18] = 6;
  pkt[19] = 4;
  pkt[20] = 0;
  pkt[21] = 2;
  memcpy(pkt + 22, my_mac, 6);
  *(uint32_t *)(pkt + 28) = my_ip;
  memcpy(pkt + 32, target_mac, 6);
  *(uint32_t *)(pkt + 38) = target_ip;
  netif_tx(pkt, 42);
}

static uint8_t arp_cache_mac[6] = {0};
static uint32_t arp_cache_ip = 0;

static void arp_request(uint32_t target_ip) {
  uint8_t pkt[42];
  memset(pkt, 0, 42);
  uint8_t bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  memcpy(pkt, bcast, 6);
  memcpy(pkt + 6, my_mac, 6);
  pkt[12] = 0x08;
  pkt[13] = 0x06;
  pkt[14] = 0;
  pkt[15] = 1;
  pkt[16] = 0x08;
  pkt[17] = 0;
  pkt[18] = 6;
  pkt[19] = 4;
  pkt[20] = 0;
  pkt[21] = 1;
  memcpy(pkt + 22, my_mac, 6);
  *(uint32_t *)(pkt + 28) = my_ip;
  memcpy(pkt + 32, bcast, 6);
  *(uint32_t *)(pkt + 38) = target_ip;
  netif_tx(pkt, 42);
}

int arp_resolve(uint32_t ip, uint8_t *out_mac) {
  extern void e1000_poll_rx(void);

  if (arp_cache_ip == ip) {
    memcpy(out_mac, arp_cache_mac, 6);
    return 0;
  }

  for (int attempt = 0; attempt < 3; attempt++) {
    arp_request(ip);
    for (int wait = 0; wait < 10000; wait++) {
      e1000_poll_rx();
      if (arp_cache_ip == ip) {
        memcpy(out_mac, arp_cache_mac, 6);
        return 0;
      }
    }
  }
  return -1;
}

void netif_rx_packet(uint8_t *pkt, int len) {
  if (len < 14)
    return;
  uint16_t ethertype = (pkt[12] << 8) | pkt[13];

  if (ethertype == 0x0806) {
    if (len >= 42) {
      uint8_t op = pkt[21];
      if (op == 2) {
        uint32_t sender_ip = *(uint32_t *)(pkt + 28);
        memcpy(arp_cache_mac, pkt + 22, 6);
        arp_cache_ip = sender_ip;
      }
      if (op == 1) {
        uint32_t target_ip = *(uint32_t *)(pkt + 38);
        if (target_ip == my_ip) {
          send_arp_reply(*(uint32_t *)(pkt + 28), pkt + 22);
        }
      }
    }
  } else if (ethertype == 0x0800) {
    if (len < 34)
      return;

    uint8_t ihl = (pkt[14] & 0x0F) * 4;
    if (ihl < 20 || 14 + ihl > len)
      return;

    uint8_t proto = pkt[23];
    uint32_t dst_ip = *(uint32_t *)(pkt + 30);
    if (dst_ip != my_ip)
      return;

    if (proto == 17) {
      uint8_t *udp = pkt + 14 + ihl;
      uint16_t src_port = (udp[0] << 8) | udp[1];
      uint16_t dst_port = (udp[2] << 8) | udp[3];
      uint16_t udp_len = (udp[4] << 8) | udp[5];

      if (udp_len < 8 || udp_len > 4096) {
        return;
      }

      uint8_t *payload = udp + 8;
      int payload_len = udp_len - 8;

#define MAX_UDP_PACKETS 8
      static struct udp_packet udp_pool[MAX_UDP_PACKETS];
      static int udp_pool_idx = 0;

      struct udp_packet *p = &udp_pool[udp_pool_idx];
      udp_pool_idx = (udp_pool_idx + 1) % MAX_UDP_PACKETS;

      p->src_ip = *(uint32_t *)(pkt + 26);
      p->src_port = src_port;
      p->dst_port = dst_port;
      p->len = payload_len;
      memcpy(p->data, payload, payload_len);
      p->next = NULL;

      if (!udp_queue_head)
        udp_queue_head = p;
      else
        udp_queue_tail->next = p;
      udp_queue_tail = p;

    } else if (proto == 6) {
      uint16_t ip_total_len = (pkt[16] << 8) | pkt[17];

      if (ip_total_len < ihl + 20 || ip_total_len > 4096) {
        return;
      }

      uint8_t *tcp = pkt + 14 + ihl;
      int tcp_len = ip_total_len - ihl;
      uint32_t src_ip = *(uint32_t *)(pkt + 26);

      extern void tcp_input(uint8_t *tcp, int len, uint32_t src_ip,
                            uint32_t dst_ip);
      tcp_input(tcp, tcp_len, src_ip, dst_ip);
    }
  }
}

int net_send_udp(uint32_t dst_ip, uint16_t src_port, uint16_t dst_port,
                 uint8_t *data, int len) {
  uint8_t pkt[1500];
  int total_len = 14 + 20 + 8 + len;
  if (total_len > 1500)
    return -1;

  uint8_t dst_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  memcpy(pkt, dst_mac, 6);
  memcpy(pkt + 6, my_mac, 6);
  pkt[12] = 0x08;
  pkt[13] = 0x00;
  pkt[14] = 0x45;
  pkt[15] = 0x00;

  uint16_t ip_len = 20 + 8 + len;
  pkt[16] = ip_len >> 8;
  pkt[17] = ip_len & 0xFF;
  pkt[18] = 0;
  pkt[19] = 1;
  pkt[20] = 0x40;
  pkt[21] = 0x00;
  pkt[22] = 64;
  pkt[23] = 17;
  pkt[24] = 0;
  pkt[25] = 0;

  *(uint32_t *)(pkt + 26) = my_ip;
  *(uint32_t *)(pkt + 30) = dst_ip;

  uint16_t csum = ip_checksum(pkt + 14, 20);
  pkt[24] = csum >> 8;
  pkt[25] = csum & 0xFF;

  uint8_t *udp = pkt + 34;
  udp[0] = src_port >> 8;
  udp[1] = src_port & 0xFF;
  udp[2] = dst_port >> 8;
  udp[3] = dst_port & 0xFF;

  uint16_t udp_len = 8 + len;
  udp[4] = udp_len >> 8;
  udp[5] = udp_len & 0xFF;
  udp[6] = 0;
  udp[7] = 0;
  memcpy(udp + 8, data, len);

  netif_tx(pkt, total_len);
  return len;
}

struct udp_packet *net_recv_udp(uint16_t port) {
  struct udp_packet *prev = NULL;
  struct udp_packet *curr = udp_queue_head;

  while (curr) {
    if (curr->dst_port == port) {
      if (prev) {
        prev->next = curr->next;
      } else {
        udp_queue_head = curr->next;
      }
      if (!curr->next) {
        udp_queue_tail = prev;
      }
      return curr;
    }
    prev = curr;
    curr = curr->next;
  }
  return NULL;
}

void net_init() {
#ifdef CONFIG_NET_E1000
  extern int e1000_is_active(void);
  if (e1000_is_active()) {
    extern uint8_t e1000_mac[6];
    memcpy(my_mac, e1000_mac, 6);
  } else
#endif
  {
    extern uint8_t mac_addr[6];
    memcpy(my_mac, mac_addr, 6);
  }

  con_puts("[NET] Stack initialized. IP: ");
  put_u64(my_ip & 0xFF);
  con_puts(".");
  put_u64((my_ip >> 8) & 0xFF);
  con_puts(".");
  put_u64((my_ip >> 16) & 0xFF);
  con_puts(".");
  put_u64((my_ip >> 24) & 0xFF);
  con_puts("\n");

  (void)my_mask;
  (void)my_gw;
}

void net_set_ip(uint32_t ip) { my_ip = ip; }

void net_poll_rx(void) {
#ifdef CONFIG_NET_E1000
  extern int e1000_is_active(void);
  extern void e1000_poll_rx(void);
  if (e1000_is_active()) {
    e1000_poll_rx();
    return;
  }
#endif

#ifdef CONFIG_NET_RTL8139
  extern int rtl8139_is_active(void);
  if (rtl8139_is_active()) {
    return;
  }
#endif
}

#endif
