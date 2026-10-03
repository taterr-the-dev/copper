#include <autoconf.h>
#ifdef CONFIG_VM
#include <kernel/console.h>
#include <kernel/pmm.h>
#include <kernel/string.h>
#include <kernel/vm.h>
#define PTE_P 0x1
#define MASK 0x000FFFFFFFFFF000ULL
#define PTE_COW 0x200ULL

extern void hex64(uint64_t v);
extern void put_u64(uint64_t v);

uint64_t boot_cr3 = 0;
void vm_init(void) {
  __asm__ volatile("mov %%cr3,%0" : "=r"(boot_cr3));
}
void vm_map(uint64_t *p4, uint64_t va, uint64_t pa, uint64_t fl) {
  int i4 = (va >> 39) & 511, i3 = (va >> 30) & 511, i2 = (va >> 21) & 511,
      i1 = (va >> 12) & 511;
  if (!(p4[i4] & PTE_P)) {
    uint64_t f = pmm_alloc();
    p4[i4] = f | 0x07;
  }
  uint64_t *p3 = (uint64_t *)(p4[i4] & MASK);
  if (!(p3[i3] & PTE_P)) {
    uint64_t f = pmm_alloc();
    p3[i3] = f | 0x07;
  }
  uint64_t *p2 = (uint64_t *)(p3[i3] & MASK);
  if ((p2[i2] & PTE_P) && (p2[i2] & 0x80)) {
    uint64_t old = p2[i2];
    uint64_t base2 = old & ~0x1FFFFFULL;
    uint64_t fl2 = (old & 0xFFF) & ~0x80ULL;
    uint64_t pt = pmm_alloc();
    uint64_t *p1t = (uint64_t *)pt;
    for (int j = 0; j < 512; j++)
      p1t[j] = (base2 + (uint64_t)j * 4096) | fl2 | 0x01;
    p2[i2] = pt | 0x07;
  }
  if (!(p2[i2] & PTE_P)) {
    uint64_t f = pmm_alloc();
    p2[i2] = f | 0x07;
  }
  uint64_t *p1 = (uint64_t *)(p2[i2] & MASK);
  p1[i1] = pa | fl;
}
uint64_t *vm_new_as(void) {
  uint64_t *p4 = (uint64_t *)pmm_alloc();
  uint64_t *p3 = (uint64_t *)pmm_alloc();
  uint64_t *p2 = (uint64_t *)pmm_alloc();
  
  for (int i = 0; i < 512; i++) {
    p4[i] = 0;
    p3[i] = 0;
    if (i < 512) {
      if (i == 0x7F7) {
        p2[i] = (uint64_t)i * 0x200000 | 0x9F;
      } else {
        p2[i] = (uint64_t)i * 0x200000 | 0x87;
      }
    }
  }

  uint64_t *p1 = (uint64_t *)pmm_alloc();
  for (int i = 0; i < 512; i++) {
    if (i == 0) {
      p1[i] = 0; 
    } else {
      p1[i] = (uint64_t)i * 0x1000 | 0x07; 
    }
  }
  p2[0] = (uint64_t)p1 | 0x07; 

  p3[0] = (uint64_t)p2 | 0x07;
  p4[0] = (uint64_t)p3 | 0x07;
  return p4;
}

uint64_t *vm_clone_as(uint64_t *src) {
  uint64_t *p4 = (uint64_t *)pmm_alloc();
  if (!p4)
    return 0;

  for (int i4 = 0; i4 < 512; i4++) {
    if (!(src[i4] & 1)) {
      p4[i4] = 0;
      continue;
    }
    if (i4 >= 256) {
      p4[i4] = src[i4];
      continue;
    }

    uint64_t *s3 = (uint64_t *)(src[i4] & MASK);
    uint64_t *d3 = (uint64_t *)pmm_alloc();
    if (!d3)
      return 0;

    for (int i3 = 0; i3 < 512; i3++) {
      if (!(s3[i3] & 1)) {
        d3[i3] = 0;
        continue;
      }
      if (s3[i3] & 0x80) {
        d3[i3] = s3[i3];
        continue;
      }

      uint64_t *s2 = (uint64_t *)(s3[i3] & MASK);
      uint64_t *d2 = (uint64_t *)pmm_alloc();
      if (!d2)
        return 0;

      for (int i2 = 0; i2 < 512; i2++) {
        if (!(s2[i2] & 1)) {
          d2[i2] = 0;
          continue;
        }

        if (s2[i2] & 0x80) {
          uint64_t *d1 = (uint64_t *)pmm_alloc();
          uint64_t *s1_new = (uint64_t *)pmm_alloc();
          if (!d1 || !s1_new)
            return 0;

          uint64_t base = s2[i2] & 0x000FFFFFE00000ULL;
          uint64_t flags = (s2[i2] & 0x1FF) & ~0x80ULL;
          uint64_t cow_flags = (flags & ~0x02) | PTE_COW;

          for (int i1 = 0; i1 < 512; i1++) {
            uint64_t pg = base + i1 * 4096;
            d1[i1] = pg | cow_flags;
            s1_new[i1] = pg | cow_flags;
          }
          d2[i2] = (uint64_t)d1 | 0x07;
          s2[i2] = (uint64_t)s1_new | 0x07;
          continue;
        }

        uint64_t *s1 = (uint64_t *)(s2[i2] & MASK);
        uint64_t *d1 = (uint64_t *)pmm_alloc();
        if (!d1)
          return 0;

        for (int i1 = 0; i1 < 512; i1++) {
          if (s1[i1] & 1) {
            uint64_t oldpg = s1[i1] & MASK;
            uint64_t flags = s1[i1] & 0xFFF;
            uint64_t cow_flags = (flags & ~0x02) | PTE_COW;

            d1[i1] = oldpg | cow_flags;
            s1[i1] = oldpg | cow_flags;
          } else
            d1[i1] = 0;
        }
        d2[i2] = (uint64_t)d1 | 0x07;
      }
      d3[i3] = (uint64_t)d2 | 0x07;
    }
    p4[i4] = (uint64_t)d3 | 0x07;
  }
  __asm__ volatile("mov %0, %%cr3" ::"r"(src) : "memory");
  return p4;
}

static inline int is_canonical(uint64_t addr) {
  return (addr >> 47) == 0 || (addr >> 47) == 0x1FFFF;
}

int vm_cow_handle(uint64_t *cr3, uint64_t va) {
  if (!cr3) {
    return 0;
  }
  if (va < 0x1000 || va >= 0x0000800000000000ULL) {
    return 0;
  }

  uint64_t p4_idx = (va >> 39) & 511;
  uint64_t p4e = cr3[p4_idx];
  if (!(p4e & 1)) {
    return 0;
  }

  uint64_t *p3 = (uint64_t *)(p4e & MASK);
  uint64_t p3_idx = (va >> 30) & 511;
  uint64_t p3e = p3[p3_idx];
  if (!(p3e & 1)) {
    return 0;
  }
  if (p3e & 0x80) {
    return 0;
  }

  uint64_t *p2 = (uint64_t *)(p3e & MASK);
  uint64_t p2_idx = (va >> 21) & 511;
  uint64_t p2e = p2[p2_idx];
  if (!(p2e & 1)) {
    return 0;
  }
  if (p2e & 0x80) {
    return 0;
  }

  uint64_t *p1 = (uint64_t *)(p2e & MASK);
  uint64_t p1_idx = (va >> 12) & 511;
  uint64_t p1e = p1[p1_idx];
  if (!(p1e & 1)) {
    return 0;
  }
  if (!(p1e & PTE_COW)) {
    return 0;
  }

  uint64_t oldpg = p1e & MASK;
  uint64_t newpg = pmm_alloc();
  if (!newpg) {
    return 0;
  }

  memcpy((void *)newpg, (void *)oldpg, 4096);
  p1[p1_idx] = newpg | ((p1e & 0xFFF) & ~PTE_COW) | 0x02;
  __asm__ volatile("invlpg (%0)" ::"r"(va) : "memory");
  return 1;
}

void vm_map_np(uint64_t *cr3, uint64_t virt, uint64_t flags) {
  vm_map(cr3, virt, 0, flags);
}

#endif

void vm_sanitize_boot_cr3(void) {
  extern uint64_t boot_cr3;
  uint64_t *p4 = (uint64_t *)boot_cr3;
  for (int i = 1; i < 512; i++)
    p4[i] = 0;
}
