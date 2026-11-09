#include <autoconf.h>
#ifdef CONFIG_KMALLOC
#include <kernel/kmalloc.h>
#include <kernel/string.h>
#include <kernel/spinlock.h>
#define HEAP_SIZE (8 * 1024 * 1024)
static uint8_t heap[HEAP_SIZE];
DEFINE_SPINLOCK(kmalloc_lock);
struct hdr {
  uint32_t size;
  uint32_t free;
  struct hdr *next;
};
static struct hdr *fl;
static int boot_heap_retired;
void kmalloc_init(void) {
  fl = (struct hdr *)heap;
  fl->size = HEAP_SIZE - sizeof(struct hdr);
  fl->free = 1;
  fl->next = 0;
  spinlock_init(&kmalloc_lock);
}
void *kmalloc(size_t n) {
  n = (n + 7) & ~7UL;
  uint64_t flags;
  void *ptr = 0;
  spin_lock_irqsave(&kmalloc_lock, &flags);
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
      ptr = (void *)(h + 1);
      break;
    }
  }
  spin_unlock_irqrestore(&kmalloc_lock, flags);
  if (ptr) {
    memset(ptr, 0, n);
    return ptr;
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
  if (boot_heap_retired && (uint8_t *)p >= heap && (uint8_t *)p < heap + HEAP_SIZE)
    return;
  uint64_t flags;
  spin_lock_irqsave(&kmalloc_lock, &flags);
  struct hdr *h = ((struct hdr *)p) - 1;
  h->free = 1;
  for (struct hdr *c = fl; c; c = c->next) {
    while (c->free && c->next && c->next->free &&
           (uint8_t *)(c + 1) + c->size == (uint8_t *)c->next) {
      c->size += sizeof(struct hdr) + c->next->size;
      c->next = c->next->next;
    }
  }
  spin_unlock_irqrestore(&kmalloc_lock, flags);
}
#ifdef CONFIG_VM
#define RELOC_PAGES 4096
extern uint64_t pmm_alloc_contig_top(size_t pages, uint64_t min_addr, uint64_t max_addr);
extern void con_puts(const char *s);
void kmalloc_relocate(void) {
  uint64_t base = pmm_alloc_contig_top(RELOC_PAGES, 0x4000000ULL, 0x40000000ULL);
  if (!base) {
    con_puts("[KMALLOC] no high region available, staying in boot heap\n");
    return;
  }
  struct hdr *a = (struct hdr *)base;
  a->size = RELOC_PAGES * 4096 - sizeof(struct hdr);
  a->free = 1;
  a->next = 0;
  uint64_t flags;
  spin_lock_irqsave(&kmalloc_lock, &flags);
  struct hdr *tail = 0;
  for (struct hdr *h = fl; h; h = h->next) {
    if ((uint8_t *)h >= heap && (uint8_t *)h < heap + HEAP_SIZE)
      h->free = 0;
    tail = h;
  }
  tail->next = a;
  boot_heap_retired = 1;
  spin_unlock_irqrestore(&kmalloc_lock, flags);
  con_puts("[OK] kmalloc: heap moved above user address range.\n");
}
#endif
#endif
