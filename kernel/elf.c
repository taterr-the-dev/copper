#include <kernel/console.h>
#include <kernel/elf.h>
#include <kernel/pmm.h>
#include <kernel/string.h>
#include <kernel/vm.h>

#define PAGE_SIZE 4096
#define PAGE_MASK (~(PAGE_SIZE - 1))
#define PAGE_ALIGN(addr) (((addr) + PAGE_SIZE - 1) & PAGE_MASK)

extern uint64_t boot_cr3;

int elf_validate(const uint8_t *img, size_t size) {
  if (size < sizeof(Elf64_Ehdr))
    return -1;
  Elf64_Ehdr *ehdr = (Elf64_Ehdr *)img;
  if (ehdr->e_ident[0] != ELFMAG0 || ehdr->e_ident[1] != ELFMAG1 ||
      ehdr->e_ident[2] != ELFMAG2 || ehdr->e_ident[3] != ELFMAG3)
    return -1;
  if (ehdr->e_ident[4] != ELFCLASS64)
    return -1;
  if (ehdr->e_ident[5] != ELFDATA2LSB)
    return -1;
  if (ehdr->e_machine != EM_X86_64)
    return -1;
  if (ehdr->e_type != ET_EXEC && ehdr->e_type != ET_DYN)
    return -1;
  if (ehdr->e_phoff + (uint64_t)ehdr->e_phnum * ehdr->e_phentsize > size)
    return -1;
  return 0;
}

static Elf64_Phdr *elf_find_phdr(Elf64_Ehdr *ehdr, uint32_t type) {
  Elf64_Phdr *phdr = (Elf64_Phdr *)((uint8_t *)ehdr + ehdr->e_phoff);
  for (int i = 0; i < ehdr->e_phnum; i++) {
    if (phdr[i].p_type == type) {
      return &phdr[i];
    }
  }
  return NULL;
}

static int elf_load_segment(const uint8_t *img, size_t img_size,
                            Elf64_Phdr *phdr, uint64_t base_addr,
                            uint64_t cr3) {
  if (phdr->p_offset + phdr->p_filesz > img_size) {
    con_puts("[ELF] segment out of bounds\n");
    return -1;
  }
  uint64_t vaddr = base_addr + phdr->p_vaddr;
  uint64_t vaddr_end = vaddr + phdr->p_memsz;
  uint64_t aligned_vaddr = vaddr & PAGE_MASK;
  uint64_t aligned_vaddr_end = PAGE_ALIGN(vaddr_end);
  for (uint64_t addr = aligned_vaddr; addr < aligned_vaddr_end;
       addr += PAGE_SIZE) {
    uint64_t page = pmm_alloc();
    if (!page) {
      con_puts("[ELF] out of memory\n");
      return -1;
    }
    uint64_t flags = 0x01;
    if (phdr->p_flags & PF_W)
      flags |= 0x02;
    if (!(phdr->p_flags & PF_X))
      flags |= 0x04;
    vm_map((uint64_t *)cr3, addr, page, flags);
    memset((void *)page, 0, PAGE_SIZE);
  }
  if (phdr->p_filesz > 0) {
    memcpy((void *)vaddr, img + phdr->p_offset, phdr->p_filesz);
  }

  return 0;
}

int elf_load_full(const uint8_t *img, size_t size, uint64_t *entry,
                  uint64_t *phdr_addr, uint64_t *phdr_num, uint64_t *phdr_size,
                  uint64_t *interp_addr, uint64_t cr3) {
  if (elf_validate(img, size) < 0) {
    return -1;
  }

  Elf64_Ehdr *ehdr = (Elf64_Ehdr *)img;
  Elf64_Phdr *phdr = (Elf64_Phdr *)(img + ehdr->e_phoff);
  int is_pie = (ehdr->e_type == ET_DYN);
  uint64_t base_addr = is_pie ? 0x400000 : 0;
  Elf64_Phdr *interp_phdr = elf_find_phdr(ehdr, PT_INTERP);
  if (interp_phdr) {
    *interp_addr = base_addr + interp_phdr->p_vaddr;
  } else {
    *interp_addr = 0;
  }
  for (int i = 0; i < ehdr->e_phnum; i++) {
    if (phdr[i].p_type == PT_LOAD) {
      if (elf_load_segment(img, size, &phdr[i], base_addr, cr3) < 0) {
        return -1;
      }
    }
  }
  *entry = base_addr + ehdr->e_entry;
  *phdr_addr = base_addr + ehdr->e_phoff;
  *phdr_num = ehdr->e_phnum;
  *phdr_size = ehdr->e_phentsize;

  return 0;
}

int elf_load_interp(const uint8_t *img, size_t size, uint64_t *entry,
                    uint64_t cr3) {
  if (elf_validate(img, size) < 0) {
    return -1;
  }

  Elf64_Ehdr *ehdr = (Elf64_Ehdr *)img;
  Elf64_Phdr *phdr = (Elf64_Phdr *)(img + ehdr->e_phoff);
  uint64_t base_addr = 0x7f0000000000ULL;
  for (int i = 0; i < ehdr->e_phnum; i++) {
    if (phdr[i].p_type == PT_LOAD) {
      if (elf_load_segment(img, size, &phdr[i], base_addr, cr3) < 0) {
        return -1;
      }
    }
  }

  *entry = base_addr + ehdr->e_entry;
  return 0;
}

int elf_load(const uint8_t *img, size_t size, uint64_t *entry) {
  uint64_t phdr_addr, phdr_num, phdr_size, interp_addr;
  return elf_load_full(img, size, entry, &phdr_addr, &phdr_num, &phdr_size,
                       &interp_addr, boot_cr3);
}

void elf_selftest(void) {
  con_puts("[ELF] selftest: ");
  uint8_t valid_elf[64] = {0};
  valid_elf[0] = 0x7f;
  valid_elf[1] = 'E';
  valid_elf[2] = 'L';
  valid_elf[3] = 'F';
  valid_elf[4] = 2;
  valid_elf[5] = 1;
  valid_elf[6] = 1;
  valid_elf[16] = 2;
  valid_elf[18] = 62;
  valid_elf[20] = 1;
  if (elf_validate(valid_elf, sizeof(valid_elf)) == 0) {
    con_puts("valid_header=OK ");
  } else {
    con_puts("valid_header=FAIL ");
  }
  uint8_t invalid_elf[64] = {0};
  if (elf_validate(invalid_elf, sizeof(invalid_elf)) < 0) {
    con_puts("invalid_magic=OK ");
  } else {
    con_puts("invalid_magic=FAIL ");
  }
  if (elf_validate(valid_elf, 10) < 0) {
    con_puts("too_small=OK\n");
  } else {
    con_puts("too_small=FAIL\n");
  }
}
