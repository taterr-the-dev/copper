#ifndef _KERNEL_PROC_H
#define _KERNEL_PROC_H
#include <kernel/types.h>
#include <kernel/spinlock.h>
#define TASK_STACK 65536
#define MAX_PROCS 64
#define USTACK_VA 0x0000700000000000ULL
#define USTACK_NP 512
#define T_RUN 1
#define T_READY 2
#define T_ZOMBIE 3
#define T_BLOCKED 4
#define T_DEAD 5

struct int_frame;

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
    uint64_t sig_pending;
    uint64_t sig_mask;
    int pgrp;
		uint64_t sleep_until;
    uint32_t tgid;
    uint64_t clear_child_tid;
    uint64_t futex_cr3;
    uint32_t futex_bitset;
    uint64_t alt_sp;
    uint64_t alt_size;
    uint32_t alt_flags;
    uint8_t kill_quiet;
    uint8_t waiting_child;
    uint8_t fpu_raw[512 + 16];
    uint8_t fxs_raw[512 + 16];
};

struct ksigaction {
    uint64_t handler;
    uint64_t flags;
    uint64_t restorer;
    uint64_t mask;
};

#define SA_NOCLDSTOP 0x00000001UL
#define SA_NOCLDWAIT 0x00000002UL
#define SA_SIGINFO   0x00000004UL
#define SA_RESTORER  0x04000000UL
#define SA_ONSTACK   0x08000000UL
#define SA_RESTART   0x10000000UL
#define SA_NODEFER   0x40000000UL
#define SA_RESETHAND 0x80000000UL

#define CLONE_VM             0x00000100UL
#define CLONE_FS             0x00000200UL
#define CLONE_FILES          0x00000400UL
#define CLONE_SIGHAND        0x00000800UL
#define CLONE_VFORK          0x00004000UL
#define CLONE_PARENT         0x00008000UL
#define CLONE_THREAD         0x00010000UL
#define CLONE_SETTLS         0x00080000UL
#define CLONE_PARENT_SETTID  0x00100000UL
#define CLONE_CHILD_CLEARTID 0x00200000UL
#define CLONE_CHILD_SETTID   0x01000000UL

extern struct task *current;
void scheduler_init(void);
int kthread_create(void (*fn)(void *), void *arg, const char *name);
void schedule(void);
void yield(void);
void kthread_exit(void);
void sched_tick(void);
int wait4(int pid, int *status);
void fork_enter(void *f);
void task_fpu_inherit(int pid);
void do_exit(int code);
int thread_create(void (*fn)(void *), void *arg, const char *name);
void do_exit_group(int code);
int futex_op(uint32_t *uaddr, int op, uint32_t val, uint64_t timeout, uint32_t *uaddr2, uint32_t val3);
int futex_wake_key(uint64_t cr3, uint64_t addr, int n, uint32_t bitset);
struct ksigaction *sigact_slot(int sig);
void proc_inherit_signals(int child_pid);
void proc_exec_prepare(void);
int proc_tgkill(int tgid, int tid, int sig);
int proc_group_post(int tgid, int sig);
int proc_kill(int pid, int sig);
int proc_getppid(void);
int64_t proc_sigreturn(void *regs);
void proc_deliver_regs(void *regs);
void proc_user_write(uint64_t addr, const void *src, uint64_t n);
void proc_thread_started(uint64_t ctid, int set_tid, int clear_tid);
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
int signal_interrupt(void);
void proc_check_signals(void);
void proc_reap_cleanup(int pid);
void deliver_signals(struct int_frame *f);
extern spinlock_t sched_lock;
#endif
void enter_usermode(uint64_t entry, uint64_t user_rsp);
int execve(const char *path, char *const *argv, char *const *envp);
void kill_current(int sig);
