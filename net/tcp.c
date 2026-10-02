#include <autoconf.h>
#include <kernel/console.h>
#include <kernel/kmalloc.h>
#include <kernel/string.h>
#include <kernel/types.h>
#include <kernel/arch.h>

#ifdef CONFIG_NET
extern void netif_tx(uint8_t *pkt, int len);
extern uint32_t my_ip;
extern void yield(void);
extern void put_u64(uint64_t v);
extern void con_puts(const char *s);

struct tcp_pcb {
  uint32_t local_ip, remote_ip;
  uint16_t local_port, remote_port;
  uint32_t seq, ack;
  uint8_t state;
  uint8_t rx_buf[16384];
  int rx_len;
  int used;
  int listen_pcb;
};
#define MAX_TCP 8
static struct tcp_pcb tcps[MAX_TCP];
static uint16_t ephemeral_port = 49152;
static uint32_t tcp_seq_base = 0x12345678;


static uint16_t swap16(uint16_t v) { return (v >> 8) | (v << 8); }
static uint32_t swap32(uint32_t v) {
  return ((v >> 24) & 0xFF) | ((v >> 8) & 0xFF00) | ((v << 8) & 0xFF0000) |
         ((v << 24) & 0xFF000000);
}

static uint16_t tcp_checksum(uint32_t src, uint32_t dst, uint8_t *tcp, int len) {
  uint32_t sum = 0;
  uint32_t src_net = swap32(src);
  uint32_t dst_net = swap32(dst);
  sum += (src_net >> 16) & 0xFFFF;
  sum += src_net & 0xFFFF;
  sum += (dst_net >> 16) & 0xFFFF;
  sum += dst_net & 0xFFFF;
  sum += 6;
  sum += len;

  for (int i = 0; i < len - 1; i += 2) {
    sum += (tcp[i] << 8) | tcp[i + 1];
  }
  if (len & 1) {
    sum += tcp[len - 1] << 8;
  }

  while (sum >> 16)
    sum = (sum & 0xFFFF) + (sum >> 16);
  return ~sum;
}

static void send_tcp(uint32_t dst_ip, uint16_t src_port, uint16_t dst_port,
                     uint32_t seq, uint32_t ack, uint8_t flags, uint8_t *data,
                     int len, uint16_t window, uint8_t *opts, int opts_len) {
  uint8_t pkt[1500];
  int tcp_hdr_len = 20 + opts_len;
  int tcp_len = tcp_hdr_len + len;
  int total_len = 14 + 20 + tcp_len;

  uint8_t dst_mac[6] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};

  memcpy(pkt, dst_mac, 6);
  extern uint8_t my_mac[6];
  memcpy(pkt + 6, my_mac, 6);
  pkt[12] = 0x08;
  pkt[13] = 0x00;
  pkt[14] = 0x45;
  pkt[15] = 0x00;
  uint16_t ip_len = 20 + tcp_len;
  pkt[16] = ip_len >> 8;
  pkt[17] = ip_len & 0xFF;
  pkt[18] = 0;
  pkt[19] = 1;
  pkt[20] = 0x40;
  pkt[21] = 0x00;
  pkt[22] = 64;
  pkt[23] = 6;

  *(uint32_t *)(pkt + 26) = my_ip;
  *(uint32_t *)(pkt + 30) = dst_ip;

  pkt[24] = 0;
  pkt[25] = 0;
  extern uint16_t ip_checksum(void *data, int len);
  uint16_t csum = ip_checksum(pkt + 14, 20);
  pkt[24] = csum >> 8;
  pkt[25] = csum & 0xFF;

  uint8_t *tcp = pkt + 34;
  tcp[0] = src_port >> 8;
  tcp[1] = src_port & 0xFF;
  tcp[2] = dst_port >> 8;
  tcp[3] = dst_port & 0xFF;
  *(uint32_t *)(tcp + 4) = swap32(seq);
  *(uint32_t *)(tcp + 8) = swap32(ack);
  
  tcp[12] = ((tcp_hdr_len / 4) << 4);
  tcp[13] = flags;
  
  tcp[14] = window >> 8;
  tcp[15] = window & 0xFF;
  
  tcp[16] = 0;
  tcp[17] = 0;
  tcp[18] = 0;
  tcp[19] = 0;
  
  if (opts && opts_len > 0) {
    memcpy(tcp + 20, opts, opts_len);
  }
  
  if (data && len > 0) {
    memcpy(tcp + tcp_hdr_len, data, len);
  }

  uint16_t tcp_csum = tcp_checksum(my_ip, dst_ip, tcp, tcp_len);
  tcp[16] = tcp_csum >> 8;
  tcp[17] = tcp_csum & 0xFF;

  if (total_len < 60) {
    memset(pkt + total_len, 0, 60 - total_len);
    total_len = 60;
  }

  netif_tx(pkt, total_len);
}

void tcp_input(uint8_t *tcp, int len, uint32_t src_ip, uint32_t dst_ip) {
  if (len < 20) return;

  uint8_t data_off = (tcp[12] >> 4) * 4;
  if (data_off < 20 || data_off > len) return;

  uint16_t src_port = (tcp[0] << 8) | tcp[1];
  uint16_t dst_port = (tcp[2] << 8) | tcp[3];
  uint32_t seq = swap32(*(uint32_t *)(tcp + 4));
  uint32_t ack_num = swap32(*(uint32_t *)(tcp + 8));
  uint8_t flags = tcp[13];

  uint8_t *payload = tcp + data_off;
  int payload_len = len - data_off;

  struct tcp_pcb *pcb = NULL;

  for (int i = 0; i < MAX_TCP; i++) {
    if (tcps[i].used && tcps[i].local_port == dst_port &&
        tcps[i].remote_ip == src_ip && tcps[i].remote_port == src_port &&
        tcps[i].state != 1) {
      pcb = &tcps[i];
      break;
    }
  }

  if (!pcb) {
    for (int i = 0; i < MAX_TCP; i++) {
      if (tcps[i].used && tcps[i].local_port == dst_port && tcps[i].state == 1) {
        pcb = &tcps[i];
        break;
      }
    }
  }

  if (!pcb) {
      return;
  }

  if (pcb->state == 2 && (flags & 0x12) == 0x12) {
    pcb->ack = seq + 1;
    uint16_t win = sizeof(pcb->rx_buf);
    send_tcp(pcb->remote_ip, pcb->local_port, pcb->remote_port, pcb->seq, pcb->ack, 0x10, NULL, 0, win, NULL, 0);
    pcb->state = 3;
    return;
  }

  if (pcb->state == 1 && (flags & 0x02)) {
    int new_idx = -1;
    for (int i = 0; i < MAX_TCP; i++) {
      if (!tcps[i].used) { new_idx = i; break; }
    }
    if (new_idx == -1) return;

    struct tcp_pcb *new_pcb = &tcps[new_idx];
    new_pcb->used = 1;
    new_pcb->state = 2;
    new_pcb->local_ip = pcb->local_ip;
    new_pcb->local_port = pcb->local_port;
    new_pcb->remote_ip = src_ip;
    new_pcb->remote_port = src_port;
    new_pcb->ack = seq + 1;
    new_pcb->seq = 1000 + new_idx;
    new_pcb->rx_len = 0;
    new_pcb->listen_pcb = pcb - tcps;

    uint16_t win = sizeof(new_pcb->rx_buf);
    send_tcp(src_ip, new_pcb->local_port, new_pcb->remote_port, new_pcb->seq, new_pcb->ack, 0x12, NULL, 0, win, NULL, 0);
    new_pcb->seq++;
    return;
  }

  if (pcb->state == 3 || pcb->state == 4) {
    if (payload_len > 0) {
      if (seq == pcb->ack) {
          int space = sizeof(pcb->rx_buf) - pcb->rx_len;
          int copy = payload_len < space ? payload_len : space;
          if (copy > 0) {
							uint64_t rflags = arch_save_flags();
              memcpy(pcb->rx_buf + pcb->rx_len, payload, copy);
              pcb->rx_len += copy;
              pcb->ack += copy;
							arch_restore_flags(rflags);
          }
      } else {
      }
      
      uint16_t win = sizeof(pcb->rx_buf) - pcb->rx_len;
      send_tcp(src_ip, pcb->local_port, pcb->remote_port, pcb->seq, pcb->ack, 0x10, NULL, 0, win, NULL, 0);
    }
    
    if (flags & 0x01) {
      pcb->ack++;
      uint16_t win = sizeof(pcb->rx_buf) - pcb->rx_len;
      send_tcp(src_ip, pcb->local_port, pcb->remote_port, pcb->seq, pcb->ack, 0x10, NULL, 0, win, NULL, 0);
      pcb->state = 4;
    }
  }
}

int tcp_listen(uint16_t port) {
  for (int i = 0; i < MAX_TCP; i++) {
    if (!tcps[i].used) {
      tcps[i].used = 1;
      tcps[i].state = 1;
      tcps[i].local_port = port;
      tcps[i].local_ip = my_ip;
      tcps[i].rx_len = 0;
      tcps[i].listen_pcb = -1;
      return i;
    }
  }
  return -1;
}

int tcp_connect(uint32_t dst_ip, uint16_t dst_port) {
  int new_idx = -1;
  for (int i = 0; i < MAX_TCP; i++) {
    if (!tcps[i].used) { new_idx = i; break; }
  }
  if (new_idx == -1) return -1;

  struct tcp_pcb *pcb = &tcps[new_idx];
  pcb->used = 1;
  pcb->state = 2;
  pcb->local_ip = my_ip;
  pcb->local_port = ephemeral_port++;
  if (ephemeral_port < 49152) ephemeral_port = 49152;
  pcb->seq = tcp_seq_base++;
  pcb->remote_ip = dst_ip;
  pcb->remote_port = dst_port;
  pcb->seq = 5000 + new_idx;
  pcb->ack = 0;
  pcb->rx_len = 0;
  pcb->listen_pcb = -1;
  
  uint16_t win = sizeof(pcb->rx_buf);
  uint8_t mss_opt[4] = {2, 4, 0x05, 0xB4}; 
  
  send_tcp(dst_ip, pcb->local_port, dst_port, pcb->seq, 0, 0x02, NULL, 0, win, mss_opt, 4);
  pcb->seq++;
  return new_idx;
}

int tcp_wait_connect(int pcb_idx) {
  extern void e1000_poll_rx(void);
  int timeout = 0;
  while (tcps[pcb_idx].state != 3) {
    e1000_poll_rx();
    yield();
    timeout++;
    if (timeout > 100000) return -1;
  }
  return 0;
}

int tcp_accept(int listen_pcb_idx) {
  extern void e1000_poll_rx(void);
  while (1) {
    e1000_poll_rx();
    for (int i = 0; i < MAX_TCP; i++) {
      if (tcps[i].used && tcps[i].listen_pcb == listen_pcb_idx && tcps[i].state == 3) {
        return i;
      }
    }
    yield();
  }
}

int tcp_read(int pcb_idx, void *buf, int len) {
  extern void e1000_poll_rx(void);
  
  while (tcps[pcb_idx].rx_len == 0 && tcps[pcb_idx].state == 3) {
    e1000_poll_rx();
    yield();
  }
  
  if (tcps[pcb_idx].rx_len == 0) {
    return 0;
  }

	uint64_t rflags = arch_save_flags();
  int copy = tcps[pcb_idx].rx_len < len ? tcps[pcb_idx].rx_len : len;
  memcpy(buf, tcps[pcb_idx].rx_buf, copy);
  memmove(tcps[pcb_idx].rx_buf, tcps[pcb_idx].rx_buf + copy, tcps[pcb_idx].rx_len - copy);
  tcps[pcb_idx].rx_len -= copy;
	arch_restore_flags(rflags);
  return copy;
}

int tcp_write(int pcb_idx, void *buf, int len) {
  if (tcps[pcb_idx].state != 3) return -1;

  uint16_t win = sizeof(tcps[pcb_idx].rx_buf) - tcps[pcb_idx].rx_len;
  send_tcp(tcps[pcb_idx].remote_ip, tcps[pcb_idx].local_port,
           tcps[pcb_idx].remote_port, tcps[pcb_idx].seq, tcps[pcb_idx].ack,
           0x18, buf, len, win, NULL, 0);
  tcps[pcb_idx].seq += len;
  return len;
}

void tcp_close(int pcb_idx) {
  if (pcb_idx >= 0 && pcb_idx < MAX_TCP) {
    if (tcps[pcb_idx].state == 3) {
      uint16_t win = sizeof(tcps[pcb_idx].rx_buf) - tcps[pcb_idx].rx_len;
      send_tcp(tcps[pcb_idx].remote_ip, tcps[pcb_idx].local_port,
               tcps[pcb_idx].remote_port, tcps[pcb_idx].seq, tcps[pcb_idx].ack,
               0x11, NULL, 0, win, NULL, 0);
      tcps[pcb_idx].seq++;
    }
    tcps[pcb_idx].used = 0;
    tcps[pcb_idx].state = 0;
  }
}
#endif
