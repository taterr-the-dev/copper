#include <autoconf.h>
#include <kernel/interrupts.h>
#ifdef CONFIG_SCHED
#include <kernel/proc.h>
extern uint64_t current_kstack;
#include <kernel/console.h>
#include <kernel/gdt.h>
#include <kernel/kmalloc.h>
#include <kernel/pmm.h>
#include <kernel/string.h>
#ifdef CONFIG_VM
#include <kernel/vm.h>
#endif
extern void put_u64(uint64_t v);
extern void hex64(uint64_t v);
extern uint8_t stack_top[];
extern void context_switch(struct task *, struct task *);
struct task *current = 0;
spinlock_t sched_lock __attribute__((aligned(64))) = SPINLOCK_INIT;
static struct task idle_task;
static uint64_t pid_map = 1;
static uint32_t pid_hint = 0;
extern void cleanup_process_fds(int pid);

static int pid_alloc_locked(void) {
  for (int i = 1; i < MAX_PROCS; i++) {
    int p = ((pid_hint + i - 1) % (MAX_PROCS - 1)) + 1;
    if (!(pid_map & (1ULL << p))) {
      pid_map |= 1ULL << p;
      pid_hint = p;
      return p;
    }
  }
  return -1;
}

static void pid_free_locked(int pid) {
  if (pid > 0 && pid < MAX_PROCS)
    pid_map &= ~(1ULL << pid);
}

static void reap_orphans_locked(void) {
  struct task *prev = current;
  struct task *t = current->next;
  while (t != current) {
    struct task *nx = t->next;
    if (t != &idle_task && t->state == T_DEAD && t->parent == 0) {
      prev->next = nx;
      pid_free_locked(t->pid);
      kfree(t->stack);
      kfree(t);
    } else {
      prev = t;
    }
    t = nx;
  }
}

static void orphan_children_locked(void) {
  struct task *t = current->next;
  while (t != current) {
    if (t->parent == current)
      t->parent = 0;
    t = t->next;
  }
}

void scheduler_init(void) {
  idle_task.kstack_top = (uint64_t)stack_top;
  idle_task.pid = 0;
  idle_task.state = 1;
  idle_task.next = &idle_task;
  memcpy(idle_task.name, "idle", 5);
  current = &idle_task;
  current_kstack = idle_task.kstack_top;
}
int kthread_create(void (*fn)(void *), void *arg, const char *name) {
  struct task *t = kmalloc(sizeof *t);
  if (!t)
    return -1;
  t->stack = kmalloc(TASK_STACK);
  if (!t->stack) {
    kfree(t);
    return -1;
  }
  uint64_t *sp = (uint64_t *)(t->stack + TASK_STACK);
  sp -= 7;
  sp[0] = 0;
  sp[1] = 0;
  sp[2] = (uint64_t)arg;
  sp[3] = (uint64_t)fn;
  sp[4] = 0;
  sp[5] = 0;
  extern void kthread_trampoline(void);
  sp[6] = (uint64_t)kthread_trampoline;
  t->ctx_rsp = (uint64_t)sp;
  t->kstack_top = (uint64_t)(t->stack + TASK_STACK);
  t->cr3 = 0;
  t->fsbase = 0;
  t->mmap_base = MMAP_BASE;
  t->parent = current;
  t->fsbase = current ? current->fsbase : 0;
  t->exit_code = 0;
  t->state = 2;
  t->fn = fn;
  t->arg = arg;
  memcpy(t->name, name, 15);
  uint64_t flags;
  spin_lock_irqsave(&sched_lock, &flags);
  reap_orphans_locked();
  int pid = pid_alloc_locked();
  if (pid < 0) {
    spin_unlock_irqrestore(&sched_lock, flags);
    kfree(t->stack);
    kfree(t);
    return -11;
  }
  t->pid = pid;
  t->next = current->next;
  current->next = t;
  spin_unlock_irqrestore(&sched_lock, flags);
  return pid;
}
void schedule(void) {
  if (!current)
    return;
  uint64_t flags;
  spin_lock_irqsave(&sched_lock, &flags);
  struct task *start = current->next, *n = start, *best = 0;
  do {
    if (n != current && (n->state == T_READY || n->state == T_RUN)) {
      best = n;
      break;
    }
    n = n->next;
  } while (n != start);
  if (best) {
#ifdef CONFIG_VM
    uint64_t c = best->cr3 ? best->cr3 : boot_cr3;
    __asm__ volatile("mov %0,%%cr3" ::"r"(c));
    __asm__ volatile("wrmsr" ::"c"(0xC0000100),
                     "a"((uint32_t)(best->fsbase & 0xFFFFFFFF)),
                     "d"((uint32_t)(best->fsbase >> 32)));
#endif
#ifdef CONFIG_TASK_TSS
    gdt_set_kernel_stack(n->kstack_top);
#endif
    current_kstack = best->kstack_top;
    __asm__ volatile("wrmsr" ::"c"(0xC0000100),
                     "a"((uint32_t)(best->fsbase & 0xFFFFFFFF)),
                     "d"((uint32_t)(best->fsbase >> 32)));
    struct task *p = current;
    current = best;
    spin_unlock(&sched_lock);
    context_switch(p, best);
    irq_restore(flags);
    return;
  }
  spin_unlock_irqrestore(&sched_lock, flags);
}
void sched_tick(void) {
  if (current)
    schedule();
}
void yield(void) {
  __asm__ volatile("cli");
  schedule();
  __asm__ volatile("sti");
}
void kthread_exit(void) {
  uint64_t flags;
  spin_lock_irqsave(&sched_lock, &flags);
  struct task *dead = current;
  dead->state = 3;
  struct task *p = dead;
  while (p->next != dead)
    p = p->next;
  struct task *nx = dead->next;
  p->next = nx;
  pid_free_locked(dead->pid);
  current = nx;
  spin_unlock(&sched_lock);
  context_switch(dead, nx);
  for (;;)
    ;
}
#endif

void do_exit(int code) {
  cleanup_process_fds(current->pid);
  uint64_t flags;
  spin_lock_irqsave(&sched_lock, &flags);
  current->exit_code = (code & 0xff) << 8;
  current->state = T_DEAD;
  orphan_children_locked();

  if (current->parent) {
    __atomic_fetch_or(&current->parent->sig_pending, 1ULL << SIGCHLD, __ATOMIC_ACQ_REL);
    if (current->parent->state == T_BLOCKED) {
      current->parent->state = T_READY;
    }
  }
  spin_unlock(&sched_lock);

  for (;;) {
    yield();
  }
}

int wait4(int pid, int *status) {
  uint64_t flags;
  spin_lock_irqsave(&sched_lock, &flags);
  for (;;) {
    int any = 0;
    struct task *t = current->next, *start = t;
    do {
      if (t->parent == current && (pid == -1 || pid == (int)t->pid)) {
        any = 1;
        if (t->state == T_DEAD) {
          struct task *p = current;
          while (p->next != t)
            p = p->next;
          p->next = t->next;
          int rpid = t->pid, code = t->exit_code;
          spin_unlock_irqrestore(&sched_lock, flags);
          kfree(t->stack);
          kfree(t);
          proc_reap_cleanup(rpid);
          uint64_t pf;
          spin_lock_irqsave(&sched_lock, &pf);
          pid_free_locked(rpid);
          spin_unlock_irqrestore(&sched_lock, pf);
          if (status)
            *status = code;
          return rpid;
        }
      }
      t = t->next;
    } while (t != start);
    if (!any) {
      spin_unlock_irqrestore(&sched_lock, flags);
      return -10;
    }
    if (signal_interrupt()) {
      spin_unlock_irqrestore(&sched_lock, flags);
      return -4;
    }
    current->state = T_BLOCKED;
    spin_unlock(&sched_lock);
    schedule();
    spin_lock(&sched_lock);
  }
}

int futex_wait(uint64_t *u, int64_t val) {
  uint64_t flags;
  spin_lock_irqsave(&sched_lock, &flags);
  if (*u != (uint64_t)val) {
    spin_unlock_irqrestore(&sched_lock, flags);
    return -11;
  }
  current->futex_addr = (uint64_t)u;
  current->state = T_BLOCKED;
  spin_unlock(&sched_lock);
  schedule();
  irq_restore(flags);
  return 0;
}
int futex_wake(uint64_t *u, int n) {
  uint64_t flags;
  spin_lock_irqsave(&sched_lock, &flags);
  int woken = 0;
  struct task *t = current->next, *start = t;
  do {
    if (t->state == T_BLOCKED && t->futex_addr == (uint64_t)u) {
      t->state = T_READY;
      t->futex_addr = 0;
      if (++woken >= n)
        break;
    }
    t = t->next;
  } while (t != start);
  spin_unlock(&sched_lock);
  __asm__ volatile("sti");
  return woken;
}
void alloc_fresh_stack(struct task *t) {
  for (int i = 0; i < USTACK_NP; i++) {
    uint64_t pg = pmm_alloc();
    if (!pg)
      return;
    memset((void *)pg, 0, 4096);
    vm_map((uint64_t *)t->cr3, USTACK_VA + i * 4096, pg, 0x07);
    t->ustack_frames[i] = pg;
  }
  t->ustack_va = USTACK_VA;
  t->ustack_np = USTACK_NP;
}

static void die_by_signal(int sig) {
  cleanup_process_fds(current->pid);
  uint64_t flags;
  spin_lock_irqsave(&sched_lock, &flags);
  current->exit_code = sig & 0x7f;
  current->state = T_DEAD;
  orphan_children_locked();
  if (current->parent) {
    __atomic_fetch_or(&current->parent->sig_pending, 1ULL << SIGCHLD, __ATOMIC_ACQ_REL);
    if (current->parent->state == T_BLOCKED) {
      current->parent->state = T_READY;
    }
  }
  spin_unlock(&sched_lock);

  for (;;) {
    yield();
  }
}

void kill_current(int sig) {
  die_by_signal(sig);
}
void send_signal(int pid, int sig) {
  uint64_t flags;
  spin_lock_irqsave(&sched_lock, &flags);
  struct task *t = current ? current->next : NULL;
  if (t) {
    struct task *start = t;
    do {
      if ((int)t->pid == pid) {
        __atomic_fetch_or(&t->sig_pending, 1ULL << sig, __ATOMIC_ACQ_REL);
        if (t->state == T_BLOCKED) {
          t->state = T_READY;
        }
        break;
      }
      t = t->next;
    } while (t != start);
  }
  spin_unlock_irqrestore(&sched_lock, flags);
}

static void task_set_state(struct task *t, int st) {
  uint64_t flags;
  spin_lock_irqsave(&sched_lock, &flags);
  t->state = st;
  spin_unlock_irqrestore(&sched_lock, flags);
}

static void task_wake_if_blocked(struct task *t) {
  uint64_t flags;
  spin_lock_irqsave(&sched_lock, &flags);
  if (t && t->state == T_BLOCKED)
    t->state = T_READY;
  spin_unlock_irqrestore(&sched_lock, flags);
}

static void handle_pending(void) {
  for (int sig = 1; sig < 64; sig++) {
    uint64_t bit = 1ULL << sig;
    if (!(current->sig_pending & bit)) continue;
    int force = (sig == SIGKILL || sig == SIGSTOP);
    if (!force && (current->sig_mask & bit)) continue;

    __atomic_fetch_and(&current->sig_pending, ~bit, __ATOMIC_ACQ_REL);
    uint64_t handler = force ? SIG_DFL : current->sig_handlers[sig];

    if (handler == SIG_IGN) continue;

    if (handler == SIG_DFL || handler == 0) {
      switch (sig) {
        case SIGCHLD:
        case SIGURG:
        case SIGWINCH:
          continue;
        case SIGSTOP:
        case SIGTSTP:
          task_set_state(current, T_BLOCKED);
          yield();
          continue;
        case SIGCONT:
          task_wake_if_blocked(current);
          continue;
        default:
          die_by_signal(sig);
          return;
      }
    }
    continue;
  }
}

int signal_interrupt(void) {
  if (!current)
    return 0;
  uint64_t unmaskable = (1ULL << SIGKILL) | (1ULL << SIGSTOP);
  uint64_t pend = current->sig_pending & ~(current->sig_mask & ~unmaskable);
  for (int sig = 1; sig < 64 && pend; sig++) {
    uint64_t bit = 1ULL << sig;
    if (!(pend & bit)) continue;
    pend &= ~bit;
    if (sig == SIGKILL || sig == SIGSTOP) return 1;
    uint64_t h = current->sig_handlers[sig];
    if (h == SIG_IGN) continue;
    if (h == SIG_DFL || h == 0) {
      if (sig == SIGCHLD || sig == SIGURG || sig == SIGWINCH || sig == SIGCONT)
        continue;
    }
    return 1;
  }
  return 0;
}

void proc_check_signals(void) {
  if (!current || !current->sig_pending) return;
  handle_pending();
}

void deliver_signals(struct int_frame *f) {
  if (!current || !current->sig_pending) return;
  if ((f->cs & 3) != 3) return;
  handle_pending();
}
