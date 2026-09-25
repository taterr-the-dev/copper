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
#endif
void enter_usermode(uint64_t entry, uint64_t user_rsp);
int execve(const char *path, char *const *argv, char *const *envp);
void kill_current(int sig);
