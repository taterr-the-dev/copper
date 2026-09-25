#include <kernel/multiboot.h>
static uint32_t *mbi;
void mb_init(uint64_t a) { mbi = (uint32_t *)a; }
int mb_module_count(void) {
  if (!mbi || !(mbi[0] & (1 << 3)))
    return 0;
  return (int)mbi[5];
}
int mb_module(int i, uint64_t *s, uint64_t *e) {
  if (i < 0 || i >= mb_module_count())
    return -1;
  uint32_t *m = (uint32_t *)(uint64_t)mbi[6];
  *s = m[i * 4 + 0];
  *e = m[i * 4 + 1];
  return 0;
}
