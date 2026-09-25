#include <autoconf.h>
#ifdef CONFIG_VM
#include <kernel/console.h>
#include <kernel/pmm.h>
#include <kernel/string.h>
#define BASE 0x800000ULL
#define SZ (256 * 1024 * 1024)
#define NF (SZ / 4096)
static uint8_t bmp[NF / 8];
void pmm_init(void) {
  for (size_t i = 0; i < sizeof(bmp); i++)
    bmp[i] = 0;
}
uint64_t pmm_alloc(void) {
  for (size_t i = 0; i < NF; i++)
    if (!(bmp[i / 8] & (1 << (i & 7)))) {
      bmp[i / 8] |= 1 << (i & 7);
      uint64_t a = BASE + i * 4096;
      memset((void *)a, 0, 4096);
      return a;
    }
  con_puts("[PMM] FATAL: Out of physical memory!\n");
  for (;;)
    __asm__ volatile("cli; hlt");
  return 0;
}
void pmm_free(uint64_t a) {
  if (a < BASE)
    return;
  size_t i = (a - BASE) / 4096;
  if (i < NF)
    bmp[i / 8] &= ~(1 << (i & 7));
}
#endif

void pmm_reserve(uint64_t lo, uint64_t hi) {
  for (uint64_t a = lo; a < hi; a += 4096) {
    if (a < BASE)
      continue;
    size_t i = (a - BASE) / 4096;
    if (i < NF)
      bmp[i / 8] |= 1 << (i & 7);
  }
}
