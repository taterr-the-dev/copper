#include <autoconf.h>
#include <kernel/console.h>
#include <kernel/fs.h>
#include <kernel/kmalloc.h>
#include <kernel/proc.h>
#include <kernel/string.h>
#include <kernel/syscall.h>
#include <kernel/types.h>

#ifdef CONFIG_NET

extern int net_send_udp(uint32_t dst_ip, uint16_t src_port, uint16_t dst_port,
                        uint8_t *data, int len);
extern void yield(void);
extern void put_u64(uint64_t v);

struct udp_packet {
  uint32_t src_ip;
  uint16_t src_port;
  uint16_t dst_port;
  int len;
  uint8_t data[1500];
  struct udp_packet *next;
};
extern struct udp_packet *net_recv_udp(uint16_t port);

struct sockaddr_in {
  uint16_t sin_family;
  uint16_t sin_port;
  uint32_t sin_addr;
  uint8_t sin_zero[8];
};

struct fdent {
  int used;
  struct fs_file f;
  char path[128];
  int dir_idx;
  int flags;
};
extern struct fdent *fdtable_of(void);

extern int tcp_listen(uint16_t port);
extern int tcp_accept(int pcb_idx);
extern int tcp_read(int pcb_idx, void *buf, int len);
extern int tcp_write(int pcb_idx, void *buf, int len);

static int tcp_pcbs[64];

int tcp_get_pcb(int fd) {
  if (fd >= 0 && fd < 64)
    return tcp_pcbs[fd];
  return -1;
}

static void port_to_str(char *buf, int port) {
  int i = 0;
  if (port == 0) {
    buf[i++] = '0';
    buf[i] = 0;
    return;
  }
  char tmp[10];
  int j = 0;
  while (port > 0) {
    tmp[j++] = '0' + (port % 10);
    port /= 10;
  }
  while (j > 0)
    buf[i++] = tmp[--j];
  buf[i] = 0;
}

static int str_to_port(const char *s) {
  int val = 0;
  while (*s >= '0' && *s <= '9') {
    val = val * 10 + (*s - '0');
    s++;
  }
  return val;
}

int64_t sys_setsockopt(int fd, int level, int optname, const void *optval,
                       int optlen) {
  (void)fd;
  (void)level;
  (void)optname;
  (void)optval;
  (void)optlen;
  return 0;
}

int64_t sys_socket(int domain, int type, int protocol) {
  (void)protocol;
  if (domain != 2)
    return -EAFNOSUPPORT;
  if (type != 2 && type != 1)
    return -ESOCKTNOSUPPORT;

  struct fdent *fdt = fdtable_of();
  for (int i = 3; i < 64; i++) {
    if (!fdt[i].used) {
      fdt[i].used = 1;
      strcpy(fdt[i].path, type == 2 ? "/net/udp/0" : "/net/tcp/0");
      fdt[i].flags = 0;
      tcp_pcbs[i] = -1;
      return i;
    }
  }
  return -EMFILE;
}

int64_t sys_bind(int fd, const struct sockaddr_in *addr, int addrlen) {
  (void)addrlen;
  struct fdent *fdt = fdtable_of();
  if (fd < 0 || fd >= 64 || !fdt[fd].used)
    return -EBADF;
  if (strncmp(fdt[fd].path, "/net/udp/", 9) != 0 &&
      strncmp(fdt[fd].path, "/net/tcp/", 9) != 0)
    return -ENOTSOCK;

  uint16_t port = (addr->sin_port >> 8) | (addr->sin_port << 8);
  char port_str[10];
  port_to_str(port_str, port);

  strcpy(fdt[fd].path, strncmp(fdt[fd].path, "/net/udp/", 9) == 0
                           ? "/net/udp/"
                           : "/net/tcp/");
  strcat(fdt[fd].path, port_str);
  return 0;
}

int64_t sys_sendto(int fd, const void *buf, size_t len, int flags,
                   const struct sockaddr_in *dest_addr, int addrlen) {
  (void)flags;
  (void)addrlen;
  struct fdent *fdt = fdtable_of();
  if (fd < 0 || fd >= 64 || !fdt[fd].used)
    return -EBADF;
  if (strncmp(fdt[fd].path, "/net/udp/", 9) != 0)
    return -ENOTSOCK;

  int src_port = str_to_port(fdt[fd].path + 9);
  uint16_t dst_port = (dest_addr->sin_port >> 8) | (dest_addr->sin_port << 8);
  uint32_t dst_ip = dest_addr->sin_addr;

  return net_send_udp(dst_ip, src_port, dst_port, (uint8_t *)buf, len);
}

int64_t sys_recvfrom(int fd, void *buf, size_t len, int flags,
                     struct sockaddr_in *src_addr, int *addrlen) {
  (void)flags;
  (void)addrlen;
  struct fdent *fdt = fdtable_of();
  if (fd < 0 || fd >= 64 || !fdt[fd].used)
    return -EBADF;
  if (strncmp(fdt[fd].path, "/net/udp/", 9) != 0)
    return -ENOTSOCK;

  int port = str_to_port(fdt[fd].path + 9);
  while (1) {
    struct udp_packet *pkt = net_recv_udp(port);
    if (pkt) {
      size_t copy_len = pkt->len < (int)len ? pkt->len : (int)len;
      memcpy(buf, pkt->data, copy_len);
      if (src_addr) {
        src_addr->sin_family = 2;
        src_addr->sin_port = (pkt->src_port >> 8) | (pkt->src_port << 8);
        src_addr->sin_addr = pkt->src_ip;
      }
      return copy_len;
    }
    yield();
  }
}

int64_t sys_listen(int fd, int backlog) {
  (void)backlog;
  struct fdent *fdt = fdtable_of();
  if (fd < 0 || fd >= 64 || !fdt[fd].used)
    return -EBADF;
  if (strncmp(fdt[fd].path, "/net/tcp/", 9) != 0)
    return -ENOTSOCK;

  int port = str_to_port(fdt[fd].path + 9);
  tcp_pcbs[fd] = tcp_listen(port);
  return tcp_pcbs[fd] >= 0 ? 0 : -1;
}

int64_t sys_accept(int fd, void *addr, int *addrlen) {
  (void)addr;
  (void)addrlen;
  struct fdent *fdt = fdtable_of();
  if (fd < 0 || fd >= 64 || !fdt[fd].used)
    return -EBADF;
  if (tcp_pcbs[fd] < 0)
    return -EINVAL;

  int pcb_idx = tcp_accept(tcp_pcbs[fd]);

  for (int i = 3; i < 64; i++) {
    if (!fdt[i].used) {
      fdt[i].used = 1;
      strcpy(fdt[i].path, "/net/tcp/est");
      tcp_pcbs[i] = pcb_idx;
      return i;
    }
  }
  return -EMFILE;
}

int64_t sys_tcp_read(int fd, void *buf, size_t len) {
  struct fdent *fdt = fdtable_of();
  if (fd < 0 || fd >= 64 || !fdt[fd].used)
    return -EBADF;
  if (tcp_pcbs[fd] < 0)
    return -EINVAL;
  return tcp_read(tcp_pcbs[fd], buf, len);
}

int64_t sys_tcp_write(int fd, const void *buf, size_t len) {
  struct fdent *fdt = fdtable_of();
  if (fd < 0 || fd >= 64 || !fdt[fd].used)
    return -EBADF;
  if (tcp_pcbs[fd] < 0)
    return -EINVAL;
  return tcp_write(tcp_pcbs[fd], (void *)buf, len);
}

int64_t sys_connect(int fd, const void *addr, int addrlen) {
  (void)addrlen;
  struct fdent *fdt = fdtable_of();
  if (fd < 0 || fd >= 64 || !fdt[fd].used) {
    return -EBADF;
  }
  if (strncmp(fdt[fd].path, "/net/tcp/", 9) != 0) {
    return -ENOTSOCK;
  }
  const struct sockaddr_in *sin = addr;
  uint32_t dst_ip = sin->sin_addr;
  uint16_t dst_port = (sin->sin_port >> 8) | (sin->sin_port << 8);
  extern int tcp_connect(uint32_t dst_ip, uint16_t dst_port);
  extern int tcp_wait_connect(int pcb_idx);
  int pcb_idx = tcp_connect(dst_ip, dst_port);
  if (pcb_idx < 0)
    return -ECONNREFUSED;
  tcp_pcbs[fd] = pcb_idx;
  int wait_ret = tcp_wait_connect(pcb_idx);
  return wait_ret;
}
#endif
