.code64
.extern syscall_dispatch
.section .bss
.align 16
.global current_kstack
current_kstack: .quad 0
.global saved_rsp
saved_rsp:    .quad 0
saved_rip:    .quad 0
saved_rflags: .quad 0
.global percpu_data
percpu_data:  .quad 0
.section .text
.global syscall_entry
syscall_entry:
    cli
    mov %rsp, saved_rsp(%rip)
    mov %rcx, saved_rip(%rip)
    mov %r11, saved_rflags(%rip)
    mov current_kstack(%rip), %rsp
    push saved_rsp(%rip)
    push saved_rip(%rip)
    push saved_rflags(%rip)
    push %r15
    push %r14
    push %r13
    push %r12
    push %r11
    push %r10
    push %r9
    push %r8
    push %rbp
    push %rdi
    push %rsi
    push %rdx
    push %rcx
    push %rbx
    push %rax
    mov %rsp, %rdi
    call syscall_dispatch
    mov %rax, debug_syscall_ret(%rip)
    mov %rax, (%rsp)
    pop %rax
    pop %rbx
    pop %rcx
    pop %rdx
    pop %rsi
    pop %rdi
    pop %rbp
    pop %r8
    pop %r9
    pop %r10
    pop %r11
    pop %r12
    pop %r13
    pop %r14
    pop %r15
    pop %r11                     /* rflags */
    pop %rcx                     /* rip */
    pop %rsp                     /* user rsp */
    sysretq
