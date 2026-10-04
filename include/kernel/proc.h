#ifndef _KERNEL_PROC_H
#define _KERNEL_PROC_H
#include <kernel/types.h>
#define TASK_STACK 65536
#define USTACK_VA 0x0000700000000000ULL
#define USTACK_NP 512
#define T_RUN 1
#define T_READY 2
#define T_ZOMBIE 3
#define T_BLOCKED 4
#define T_DEAD 5

struct signal_frame {
    union {
        struct {
            uint64_t rax, rbx, rcx, rdx, rsi, rdi, rbp;
            uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
            uint64_t rip, rsp, rflags;
            uint64_t sig;
        };
        uint64_t raw[20];
    };
};

struct task {
    uint64_t ctx_rsp;
    uint32_t pid;
    uint8_t state;
    void (*fn)(void *);
    void *arg;
    uint8_t *stack;
    uint64_t kstack_top;
    uint64_t cr3;
    struct task *parent;
    int exit_code;
    uint64_t fsbase;
    uint64_t futex_addr;
    uint64_t mmap_base;
    uint64_t ustack_va;
    uint64_t ustack_frames[USTACK_NP];
    int ustack_np;
    char name[16];
    struct task *next;
    uint64_t sig_handlers[64];
    uint64_t sig_pending;
    uint64_t sig_mask;
    int pgrp;
		uint64_t sleep_until;
};

extern struct task *current;
void scheduler_init(void);
int kthread_create(void (*fn)(void *), void *arg, const char *name);
void schedule(void);
void yield(void);
void kthread_exit(void);
void sched_tick(void);
int wait4(int pid, int *status);
void fork_enter(void *f);
void do_exit(int code);
int futex_wait(uint64_t *u, int64_t val);
int futex_wake(uint64_t *u, int n);

#define SIGHUP    1
#define SIGINT    2
#define SIGQUIT   3
#define SIGILL    4
#define SIGTRAP   5
#define SIGABRT   6
#define SIGBUS    7
#define SIGFPE    8
#define SIGKILL   9
#define SIGUSR1   10
#define SIGSEGV   11
#define SIGUSR2   12
#define SIGPIPE   13
#define SIGALRM   14
#define SIGTERM   15
#define SIGSTKFLT 16
#define SIGCHLD   17
#define SIGCONT   18
#define SIGSTOP   19
#define SIGTSTP   20
#define SIGTTIN   21
#define SIGTTOU   22
#define SIGURG    23
#define SIGXCPU   24
#define SIGXFSZ   25
#define SIGVTALRM 26
#define SIGPROF   27
#define SIGWINCH  28
#define SIGIO     29
#define SIGPWR    30
#define SIGSYS    31

#define SIG_DFL   0
#define SIG_IGN   1

#define SIG_BLOCK    0
#define SIG_UNBLOCK  1
#define SIG_SETMASK  2

struct int_frame;

void send_signal(int pid, int sig);
void deliver_signals(struct int_frame *f);
#endif
void enter_usermode(uint64_t entry, uint64_t user_rsp);
int execve(const char *path, char *const *argv, char *const *envp);
void kill_current(int sig);
