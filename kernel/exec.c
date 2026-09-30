#include <autoconf.h>
#include <kernel/console.h>
#include <kernel/elf.h>
#include <kernel/fs.h>
#include <kernel/kmalloc.h>
#include <kernel/pmm.h>
#include <kernel/proc.h>
#include <kernel/string.h>
#include <kernel/vm.h>
#include <kernel/syscall.h>

extern uint64_t boot_cr3;
extern void enter_usermode(uint64_t entry, uint64_t sp);
extern void reset_mm_state(int pid);
extern void put_u64(uint64_t v);
extern void hex64(uint64_t v);
#define EXEC_BUF_SIZE (4 * 1024 * 1024)
#define EXEC_BUF_VA 0x80000000ULL

static uint8_t *exec_buf = NULL;

static uint8_t *exec_buf_get(void) {
  if (exec_buf)
    return exec_buf;
  for (int i = 0; i < EXEC_BUF_SIZE / 4096; i++) {
    uint64_t pg = pmm_alloc();
    if (!pg)
      return NULL;
    vm_map((uint64_t *)boot_cr3, EXEC_BUF_VA + i * 4096, pg, 0x07);
  }
  exec_buf = (uint8_t *)EXEC_BUF_VA;
  return exec_buf;
}

__attribute__((unused)) static void phx(uint64_t v) {
  for (int i = 15; i >= 0; i--) {
    int d = (v >> (i * 4)) & 15;
    con_putc(d < 10 ? '0' + d : 'a' + d - 10);
  }
}

static uint64_t load_mem(uint8_t *b, uint64_t *cr3, uint64_t base, char *interp,
                         size_t interpsz) {
  Elf64_Ehdr *e = (Elf64_Ehdr *)b;
  Elf64_Phdr *ph = (Elf64_Phdr *)(b + e->e_phoff);

  if (interp) {
    for (int i = 0; i < e->e_phnum; i++) {
      if (ph[i].p_type == PT_INTERP && ph[i].p_filesz < interpsz) {
        memcpy(interp, b + ph[i].p_offset, ph[i].p_filesz);
        interp[ph[i].p_filesz] = 0;
        break;
      }
    }
  }

  for (int i = 0; i < e->e_phnum; i++) {
    if (ph[i].p_type != PT_LOAD)
      continue;

    uint64_t va = base + ph[i].p_vaddr;
    uint64_t s = va & ~0xFFFULL, en = (va + ph[i].p_memsz + 0xFFF) & ~0xFFFULL;

    int num_pages = (en - s) / 4096;
    uint64_t *pages = kmalloc(sizeof(uint64_t) * num_pages);
    if (!pages)
      return 0;

    for (int j = 0; j < num_pages; j++) {
      uint64_t pg = pmm_alloc();
      if (!pg) {
        kfree(pages);
        return 0;
      }
      uint64_t target_va = s + j * 4096;
      vm_map(cr3, target_va, pg, 0x07);
      memset((void *)pg, 0, 4096);
      pages[j] = pg;
    }

    if (ph[i].p_filesz > 0) {
      uint64_t src = (uint64_t)(b + ph[i].p_offset);
      uint64_t remaining = ph[i].p_filesz;
      uint64_t offset_in_seg = 0;

      while (remaining > 0) {
        uint64_t current_va = va + offset_in_seg;
        int page_idx = (current_va - s) / 4096;
        uint64_t pg = pages[page_idx];
        uint64_t off = current_va & 0xFFF;

        uint64_t copy_len = 4096 - off;
        if (copy_len > remaining)
          copy_len = remaining;
        memcpy((void *)(pg + off), (void *)(src + offset_in_seg), copy_len);

        offset_in_seg += copy_len;
        remaining -= copy_len;
      }
    }

    if (ph[i].p_memsz > ph[i].p_filesz) {
      uint64_t bss_start = ph[i].p_filesz;
      uint64_t bss_end = ph[i].p_memsz;
      uint64_t remaining = bss_end - bss_start;
      uint64_t offset_in_seg = bss_start;

      while (remaining > 0) {
        uint64_t current_va = va + offset_in_seg;
        int page_idx = (current_va - s) / 4096;
        uint64_t pg = pages[page_idx];
        uint64_t off = current_va & 0xFFF;

        uint64_t zero_len = 4096 - off;
        if (zero_len > remaining)
          zero_len = remaining;
        memset((void *)(pg + off), 0, zero_len);

        offset_in_seg += zero_len;
        remaining -= zero_len;
      }
    }
    kfree(pages);
  }
  return base + e->e_entry;
}

static uint64_t copy_str_to_user(uint64_t *frames, int num_frames,
                                 uint64_t *top, const char *str) {
  size_t len = strlen(str) + 1;
  *top -= len;
  uint64_t user_addr = *top;
  uint64_t offset_in_stack = user_addr - USTACK_VA;
  int frame_idx = offset_in_stack / 4096;
  uint64_t offset_in_frame = offset_in_stack % 4096;
  if (frame_idx >= num_frames)
    return 0;
  uint64_t pg = frames[frame_idx];
  memcpy((void *)(pg + offset_in_frame), str, len);
  return user_addr;
}

int execve(const char *path, char *const *argv, char *const *envp) {
  static char resolved_path[256];
  static char k_path[256];

  resolve_path(path, resolved_path, sizeof(resolved_path));

  int i = 0;
  while (i < 255 && resolved_path[i]) {
    k_path[i] = resolved_path[i];
    i++;
  }
  k_path[i] = 0;

  static const char *k_argv[32];
  static char k_argv_strs[32][256];
  int argc = 0;
  if (argv) {
    while (argc < 31 && argv[argc]) {
      int j = 0;
      while (j < 255 && argv[argc][j]) {
        k_argv_strs[argc][j] = argv[argc][j];
        j++;
      }
      k_argv_strs[argc][j] = 0;
      k_argv[argc] = k_argv_strs[argc];
      argc++;
    }
  } else {
    argc = 1;
    int j = 0;
    while (j < 255 && resolved_path[j]) {
      k_argv_strs[0][j] = resolved_path[j];
      j++;
    }
    k_argv_strs[0][j] = 0;
    k_argv[0] = k_argv_strs[0];
  }

  static const char *k_envp[32];
  static char k_envp_strs[32][256];
  int envc = 0;
  if (envp) {
    while (envc < 31 && envp[envc]) {
      int j = 0;
      while (j < 255 && envp[envc][j]) {
        k_envp_strs[envc][j] = envp[envc][j];
        j++;
      }
      k_envp_strs[envc][j] = 0;
      k_envp[envc] = k_envp_strs[envc];
      envc++;
    }
  }

  uint64_t saved_cr3;
  __asm__ volatile("mov %%cr3, %0" : "=r"(saved_cr3));
  __asm__ volatile("mov %0,%%cr3" ::"r"(boot_cr3) : "memory");
  uint64_t *new_cr3 = vm_new_as();
  __asm__ volatile("mov %0,%%cr3" ::"r"(saved_cr3) : "memory");

  if (!new_cr3) {
    return -1;
  }

  struct fs_file f;
  int open_res = vfs_open(k_path, &f);

  if (open_res < 0) {
    __asm__ volatile("mov %0,%%cr3" ::"r"(saved_cr3) : "memory");
    return -ENOENT;
  }
  if (f.size > EXEC_BUF_SIZE) {
    vfs_close(&f);
    __asm__ volatile("mov %0,%%cr3" ::"r"(saved_cr3) : "memory");
    return -1;
  }

  if (!exec_buf_get()) {
    vfs_close(&f);
    __asm__ volatile("mov %0,%%cr3" ::"r"(saved_cr3) : "memory");
    return -1;
  }

  int64_t read_sz = vfs_read(&f, exec_buf, f.size);
  vfs_close(&f);

  if (read_sz < 0) {
    __asm__ volatile("mov %0,%%cr3" ::"r"(saved_cr3) : "memory");
    return -1;
  }

  if (exec_buf[0] == '#' && exec_buf[1] == '!') {
    char interp[128];
    int idx = 2, o = 0;
    while (idx < 128 && exec_buf[idx] == ' ')
      idx++;
    while (idx < 128 && exec_buf[idx] != '\n' && exec_buf[idx] != ' ' &&
           o < 127)
      interp[o++] = exec_buf[idx++];
    interp[o] = 0;
    return execve(interp, (char *const *)k_argv, (char *const *)k_envp);
  }

  Elf64_Ehdr *e = (Elf64_Ehdr *)exec_buf;
  if (e->e_ident[0] != 0x7f || e->e_ident[1] != 'E' || e->e_ident[2] != 'L' ||
      e->e_ident[3] != 'F') {
    __asm__ volatile("mov %0,%%cr3" ::"r"(saved_cr3) : "memory");
    return -1;
  }

  uint64_t main_base = (e->e_type == ET_DYN) ? 0x400000 : 0;
  uint64_t main_phoff = e->e_phoff;
  uint16_t main_phnum = e->e_phnum;
  uint16_t main_phentsize = e->e_phentsize;
  uint64_t main_entry = e->e_entry;
  uint8_t main_phdrs_buf[4096];
  size_t main_phdrs_size = main_phnum * main_phentsize;
  if (main_phdrs_size > 4096) main_phdrs_size = 4096;
  memcpy(main_phdrs_buf, exec_buf + main_phoff, main_phdrs_size);
  Elf64_Phdr *ph_buf = (Elf64_Phdr *)(exec_buf + e->e_phoff);
  for (int i = 0; i < e->e_phnum; i++) {
    if (ph_buf[i].p_type == PT_TLS && ph_buf[i].p_align == 0) {
      ph_buf[i].p_align = 8;
    }
  }

  uint64_t base = (e->e_type == ET_DYN) ? 0x400000 : 0;
  static char interp_path[256];
  interp_path[0] = 0;
  uint64_t entry =
      load_mem(exec_buf, new_cr3, base, interp_path, sizeof interp_path);

  if (!entry) {
    __asm__ volatile("mov %0,%%cr3" ::"r"(saved_cr3) : "memory");
    return -1;
  }
  uint64_t interp_base = 0, interp_entry = 0;
  if (interp_path[0]) {
    struct fs_file if_;
    if (vfs_open(interp_path, &if_) == 0 && if_.size <= EXEC_BUF_SIZE) {
      vfs_read(&if_, exec_buf, if_.size);
      vfs_close(&if_);
      Elf64_Ehdr *ie = (Elf64_Ehdr *)exec_buf;
      Elf64_Phdr *iph = (Elf64_Phdr *)(exec_buf + ie->e_phoff);
      for (int i = 0; i < ie->e_phnum; i++) {
        if (iph[i].p_type == PT_TLS && iph[i].p_align == 0) {
          iph[i].p_align = 8;
        }
      }
      interp_base = 0x7f0000000000ULL;
      interp_entry = load_mem(exec_buf, new_cr3, interp_base, 0, 0);
    }
  }
  static uint64_t frames[USTACK_NP];
  for (int i = 0; i < USTACK_NP; i++) {
    frames[i] = pmm_alloc();
    if (!frames[i]) {
      __asm__ volatile("mov %0,%%cr3" ::"r"(saved_cr3) : "memory");
      return -1;
    }
    vm_map(new_cr3, USTACK_VA + i * 4096, frames[i], 0x07);
    memset((void *)frames[i], 0, 4096);
  }

  uint64_t stacktop = USTACK_VA + USTACK_NP * 4096;
  static uint64_t aptr[32], eptr[32];
  uint64_t top = stacktop;
  for (int i = 0; i < argc; i++) {
    aptr[i] = copy_str_to_user(frames, USTACK_NP, &top, k_argv[i]);
  }
  for (int i = 0; i < envc; i++) {
    eptr[i] = copy_str_to_user(frames, USTACK_NP, &top, k_envp[i]);
  }
  top -= 16;
  uint64_t randp = top;
  {
    uint64_t offset_in_stack = randp - USTACK_VA;
    int frame_idx = offset_in_stack / 4096;
    uint64_t offset_in_frame = offset_in_stack % 4096;
    uint64_t pg = frames[frame_idx];
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    uint64_t seed = ((uint64_t)hi << 32) | lo;
    uint8_t *rand_buf = (uint8_t *)(pg + offset_in_frame);
    for (int i = 0; i < 16; i++) {
      seed = seed * 6364136223846793005UL + 1442695040888963407UL;
      rand_buf[i] = (uint8_t)(seed >> 33);
    }
  }

  uint64_t phdr_vaddr = main_base + main_phoff;

  if (phdr_vaddr > 0 && phdr_vaddr < 0x1000) {
    uint64_t phdr_pg = pmm_alloc();
    if (phdr_pg) {
      vm_map(new_cr3, 0, phdr_pg, 0x07);
      memset((void *)phdr_pg, 0, 4096);
      memcpy((void *)(phdr_pg + phdr_vaddr), main_phdrs_buf, main_phdrs_size);
    }
  }

  static uint64_t auxv[64];
  int ai = 0;
  auxv[ai++] = AT_PHDR;
  auxv[ai++] = phdr_vaddr;
  auxv[ai++] = AT_PHENT;
  auxv[ai++] = sizeof(Elf64_Phdr);
  auxv[ai++] = AT_PHNUM;
  auxv[ai++] = e->e_phnum;
  auxv[ai++] = AT_PAGESZ;
  auxv[ai++] = 4096;
  auxv[ai++] = AT_CLKTCK;
  auxv[ai++] = 100;
  auxv[ai++] = AT_BASE;
  auxv[ai++] = interp_base;
  auxv[ai++] = AT_ENTRY;
  auxv[ai++] = main_base + main_entry;
  auxv[ai++] = AT_RANDOM;
  auxv[ai++] = randp;
  auxv[ai++] = AT_SECURE;
  auxv[ai++] = 0;
  auxv[ai++] = AT_UID;
  auxv[ai++] = 0;
  auxv[ai++] = AT_EUID;
  auxv[ai++] = 0;
  auxv[ai++] = AT_GID;
  auxv[ai++] = 0;
  auxv[ai++] = AT_EGID;
  auxv[ai++] = 0;
  auxv[ai++] = AT_SYSINFO_EHDR;
  auxv[ai++] = 0;
  auxv[ai++] = AT_NULL;
  auxv[ai++] = 0;
  uint64_t vecsize = 8 * ((1 + argc + 1 + envc + 1) + (uint64_t)ai);
  uint64_t R = (top - vecsize) & ~0xFULL;

  uint64_t offset_in_stack = R - USTACK_VA;
  int frame_idx = offset_in_stack / 4096;
  uint64_t offset_in_frame = offset_in_stack % 4096;
  uint64_t pg = frames[frame_idx];

  uint64_t *p = (uint64_t *)(pg + offset_in_frame);
  *p++ = argc;
  for (int i = 0; i < argc; i++)
    *p++ = aptr[i];
  *p++ = 0;
  for (int i = 0; i < envc; i++)
    *p++ = eptr[i];
  *p++ = 0;
  memcpy(p, auxv, ai * 8);

  uint64_t final_entry = interp_entry ? interp_entry : entry;

  current->cr3 = (uint64_t)new_cr3;
  current->ustack_va = USTACK_VA;
  current->ustack_np = USTACK_NP;
  for (int i = 0; i < USTACK_NP; i++) {
    current->ustack_frames[i] = frames[i];
  }

  uint64_t tls_page = pmm_alloc();
  if (tls_page) {
    uint64_t tls_va = 0x00007FFF80000000ULL;
    vm_map(new_cr3, tls_va, tls_page, 0x07);
    memset((void *)tls_page, 0, 4096);

    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    uint64_t canary = ((uint64_t)hi << 32) | lo;
    *(uint64_t *)(tls_page + 0x28) = canary;

    current->fsbase = tls_va;
    __asm__ volatile("wrmsr" ::"c"(0xC0000100),
                     "a"((uint32_t)(tls_va & 0xFFFFFFFF)),
                     "d"((uint32_t)(tls_va >> 32))
                     : "memory");
  }

  __asm__ volatile("mov %0, %%cr3" ::"r"((uint64_t)new_cr3) : "memory");
  reset_mm_state(current->pid);
  enter_usermode(final_entry, R);
  return 0;
}
