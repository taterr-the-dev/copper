.section .multiboot, "a"
.align 8
mboot1_start:
    .long 0x1BADB002
    .long 0x00010003
    .long -(0x1BADB002 + 0x00010003)
    .long mboot1_start
    .long 0x100000
    .long _load_end
    .long _kernel_end
    .long _start
.section .text
.code32
.global _start
.extern kernel_main
_start:
    cli
    mov $stack_top, %esp
    mov %eax, %edi
    mov %ebx, %esi
    cmp $0x2BADB002, %eax
    jne .halt
    pushf
    pop %eax
    mov %eax, %ecx
    xor $0x200000, %eax
    push %eax
    popf
    pushf
    pop %eax
    push %ecx
    popf
    cmp %ecx, %eax
    je .halt
    mov $0x80000000, %eax
    cpuid
    cmp $0x80000001, %eax
    jb .halt
    mov $0x80000001, %eax
    cpuid
    test $0x20000000, %edx
    jz .halt
    mov $pml4, %eax
    mov %eax, %cr3
    mov $pdpt, %eax
    or $0x07, %eax
    mov %eax, pml4
    mov $pd, %eax
    or $0x07, %eax
    mov %eax, pdpt
    # Fill PD: 512 x 2MB pages = 1GB identity map
    mov $pd, %ebp
    mov $0x87, %eax
    xor %ecx, %ecx
1:
    mov %eax, (%ebp)
    add $0x200000, %eax
    add $8, %ebp
    inc %ecx
    cmp $512, %ecx
    jb 1b
    mov %cr4, %eax
    or $0x20, %eax
    mov %eax, %cr4
    mov $0xC0000080, %ecx
    rdmsr
    or $0x100, %eax
    wrmsr
    mov %cr0, %eax
    or $0x80000000, %eax
    mov %eax, %cr0
    lgdt gdt64_pointer
    ljmp $0x08, $long_mode_start
.halt:
    hlt
    jmp .halt
.code64
long_mode_start:
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss
    mov %edi, %edi
    mov %esi, %esi
    call kernel_main
.halt64:
    cli
    hlt
    jmp .halt64
.section .data
.align 4096
pml4:  .space 4096, 0
pdpt:  .space 4096, 0
pd:    .space 4096, 0
.section .bss
.align 16
stack_bottom:
    .space 16384
.global stack_top
stack_top:
.section .rodata
gdt64:
    .quad 0
gdt64_code:
    .quad 0x00AF9A000000FFFF
gdt64_data:
    .quad 0x00CF92000000FFFF
gdt64_pointer:
    .short gdt64_pointer - gdt64 - 1
    .quad gdt64
