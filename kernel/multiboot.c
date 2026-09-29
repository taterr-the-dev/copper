#include <autoconf.h>
#include <kernel/console.h>
#include <kernel/pmm.h>
#include <kernel/string.h>

static uint32_t *mbi;

void mb_init(uint64_t a) {
    mbi = (uint32_t *)a;
    uint32_t flags = mbi[0];
    con_puts("[MB] Parsing Multiboot1 Memory Map...\n");
    if (flags & (1 << 6)) {
        uint32_t mmap_length = mbi[11];
        uint32_t mmap_addr = mbi[12];
        uint8_t *mmap = (uint8_t *)(uint64_t)mmap_addr;
        uint8_t *end = mmap + mmap_length;
        while (mmap < end) {
            uint32_t entry_size = *(uint32_t *)mmap;
            uint64_t addr = *(uint64_t *)(mmap + 4);
            uint64_t len = *(uint64_t *)(mmap + 12);
            uint32_t type = *(uint32_t *)(mmap + 20);
            if (type == 1) {
                pmm_add_region(addr, len);
            } else {
                pmm_reserve_region(addr, len);
            }
            mmap += entry_size + 4;
        }
    } else {
        con_puts("[MB] WARNING: No memory map provided by bootloader!\n");
    }
}

int mb_module_count(void) {
  if (!mbi || !(mbi[0] & (1 << 3))) return 0;
  return (int)mbi[4];
}

int mb_module(int i, uint64_t *s, uint64_t *e) {
  if (i < 0 || i >= mb_module_count()) return -1;
  uint32_t *m = (uint32_t *)(uint64_t)mbi[5];
  *s = m[i * 3 + 0];
  *e = m[i * 3 + 1];
  return 0;
}
