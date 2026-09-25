#include <autoconf.h>
#ifdef CONFIG_KMALLOC
#include <kernel/kmalloc.h>
#include <kernel/string.h>
#define HEAP_SIZE (8 * 1024 * 1024)
static uint8_t heap[HEAP_SIZE];
struct hdr {
  uint32_t size;
  uint32_t free;
  struct hdr *next;
};
static struct hdr *fl;
void kmalloc_init(void) {
  fl = (struct hdr *)heap;
  fl->size = HEAP_SIZE - sizeof(struct hdr);
  fl->free = 1;
  fl->next = 0;
}
void *kmalloc(size_t n) {
  n = (n + 7) & ~7UL;
  for (struct hdr *h = fl; h; h = h->next) {
    if (h->free && h->size >= n) {
      if (h->size >= n + sizeof(struct hdr) + 8) {
        struct hdr *nh = (struct hdr *)((uint8_t *)(h + 1) + n);
        nh->size = h->size - n - sizeof(struct hdr);
        nh->free = 1;
        nh->next = h->next;
        h->size = n;
        h->next = nh;
      }
      h->free = 0;
	  memset(h + 1, 0, n);
      return (void *)(h + 1);
    }
  }
  extern void con_puts(const char *s);
  extern void put_u64(uint64_t v);
  con_puts("[KMALLOC] OUT OF MEMORY! Requested: ");
  put_u64(n);
  con_puts(" bytes\n");
  return 0;
}
void kfree(void *p) {
  if (!p)
    return;
  struct hdr *h = ((struct hdr *)p) - 1;
  h->free = 1;
  for (struct hdr *c = fl; c; c = c->next) {
    while (c->free && c->next && c->next->free &&
           (uint8_t *)(c + 1) + c->size == (uint8_t *)c->next) {
      c->size += sizeof(struct hdr) + c->next->size;
      c->next = c->next->next;
    }
  }
}
#endif
