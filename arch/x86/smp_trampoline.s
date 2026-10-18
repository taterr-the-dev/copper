.code16
.global trampoline_start
.global trampoline_end
.extern smp_ap_c_entry

.global off_cpu_id
.global off_stack
.global off_cr3

.set off_cpu_id, cpu_id_ptr - trampoline_start
.set off_stack,  stack_ptr  - trampoline_start
.set off_cr3,    cr3_ptr    - trampoline_start

trampoline_start:
    cli
    movl $0x10000001, (0x7FF0)
    .byte 0x67, 0x66, 0x0f, 0x01, 0x15
    .long (0x7000 + (gdt_desc - trampoline_start))
    .byte 0x67, 0x66, 0x0f, 0x01, 0x1d
    .long (0x7000 + (idt_desc - trampoline_start))
    movl $0x10000002, (0x7FF0)
    mov %cr0, %eax
    or  $1, %al
    mov %eax, %cr0
    movl $0x10000003, (0x7FF0)
    .byte 0x66, 0xEA
    .long (0x7000 + (pm32_entry - trampoline_start))
    .word 0x0008

.code32
pm32_entry:
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    mov %ax, %fs
    mov %ax, %gs
    movl $0x10000004, (0x7FF0)
    mov %cr4, %eax
    or  $0x20, %eax
    mov %eax, %cr4
    mov $0x8000, %eax
    mov %eax, %cr3
    movl $0x10000005, (0x7FF0)
    mov $0xC0000080, %ecx
    rdmsr
    or  $0x100, %eax
    wrmsr
    movl $0x10000006, (0x7FF0)
    mov %cr0, %eax
    or  $0x80000000, %eax
    mov %eax, %cr0
    movl $0x10000007, (0x7FF0)
    .byte 0xEA
    .long (0x7000 + (lm64_entry - trampoline_start))
    .word 0x0018

.code64
lm64_entry:
    mov $0x10, %eax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    movl $0x10000008, (0x7FF0)
    movabs $0x7000 + (cpu_id_ptr - trampoline_start), %rax
    mov (%rax), %edi
    movabs $0x7000 + (stack_ptr - trampoline_start), %rax
    mov (%rax), %rsp
    movabs $0x7000 + (cr3_ptr - trampoline_start), %rax
    mov (%rax), %rsi
    movl $0x10000009, (0x7FF0)
    mov %rsi, %cr3
    movabs $smp_ap_c_entry, %rax
    jmp  *%rax
    .p2align 4
gdt:
    .quad 0x0000000000000000
    .quad 0x00CF9A000000FFFF
    .quad 0x00CF92000000FFFF
    .quad 0x00AF9A000000FFFF

gdt_desc:
    .word gdt_desc - gdt - 1
    .long 0x7000 + (gdt - trampoline_start)

idt_desc:
    .word 0
    .long 0

cpu_id_ptr:
    .long 0
    .p2align 3
stack_ptr:
    .quad 0
cr3_ptr:
    .quad 0

trampoline_end:
