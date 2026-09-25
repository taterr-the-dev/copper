.code64
.global context_switch
context_switch:
    push %rbp
    push %rbx
    push %r12
    push %r13
    push %r14
    push %r15
    mov %rsp, (%rdi)      # prev->ctx_rsp = rsp
    mov (%rsi), %rsp      # rsp = next->ctx_rsp
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %rbx
    pop %rbp
    ret
.global kthread_trampoline
kthread_trampoline:
    sti                   # re-enable IRQs for a fresh thread
    mov %r13, %rdi        # arg
    call *%r12            # fn(arg)
    call kthread_exit
