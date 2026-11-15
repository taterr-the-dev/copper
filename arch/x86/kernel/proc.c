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
#include <kernel/syscall.h>
#ifdef CONFIG_VM
#include <kernel/vm.h>
#endif
extern void put_u64(uint64_t v);
extern void hex64(uint64_t v);
extern uint8_t stack_top[];
static uint8_t fpu_template[512 + 16];

static inline uint8_t *fpu_area(struct task *t) {
  return (uint8_t *)(((uint64_t)t->fpu_raw + 15) & ~15ULL);
}
static inline uint8_t *fx_scratch(struct task *t) {
  return (uint8_t *)(((uint64_t)t->fxs_raw + 15) & ~15ULL);
}
static inline uint8_t *fpu_tmpl(void) {
  return (uint8_t *)(((uint64_t)fpu_template + 15) & ~15ULL);
}
static inline void fpu_save(struct task *t) {
  __asm__ volatile("fxsave (%0)" ::"r"(fpu_area(t)) : "memory");
}
static inline void fpu_load(struct task *t) {
  __asm__ volatile("fxrstor (%0)" ::"r"(fpu_area(t)) : "memory");
}
extern void context_switch(struct task *, struct task *);
struct task *current = 0;
spinlock_t sched_lock __attribute__((aligned(64))) = SPINLOCK_INIT;
static struct task idle_task;
static uint64_t pid_map = 1;
static uint8_t group_nthreads[MAX_PROCS];
static uint8_t group_exiting[MAX_PROCS];
static struct ksigaction *sigtab[MAX_PROCS];
static struct ksigaction sig_dummy[64];
#define USER_LIM 0x0000800000000000ULL
static uint32_t pid_hint = 0;
extern void cleanup_process_fds(int pid);
extern void proc_reap_cleanup(int pid);

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

static void release_mm(void) {
  uint64_t cr3 = current->cr3;
  if (!cr3 || cr3 == boot_cr3)
    return;
  current->cr3 = 0;
  __asm__ volatile("mov %0,%%cr3" ::"r"(boot_cr3) : "memory");
  vm_free_as((uint64_t *)cr3);
}

static void free_sigtab(int pid) {
  if (pid > 0 && pid < MAX_PROCS && sigtab[pid]) {
    kfree(sigtab[pid]);
    sigtab[pid] = 0;
  }
}

static void reap_orphans_locked(void) {
  struct task *prev = current;
  struct task *t = current->next;
  while (t != current) {
    struct task *nx = t->next;
    int leader = (t->pid == t->tgid);
    if (t != &idle_task && t->state == T_DEAD && t->parent == 0 &&
        (!leader || group_nthreads[t->pid] == 0)) {
      prev->next = nx;
      if (leader) {
        proc_reap_cleanup(t->pid);
        free_sigtab(t->pid);
      }
      pid_free_locked(t->pid);
      kfree(t->stack);
      kfree(t);
    } else {
      prev = t;
    }
    t = nx;
  }
}
static void orphan_children_locked(struct task *dead) {
  struct task *t = current->next;
  while (t != current) {
    if (t->parent == dead)
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
  __asm__ volatile("fxsave (%0)" ::"r"(fpu_tmpl()) : "memory");
  memcpy(fpu_area(&idle_task), fpu_tmpl(), 512);
  current = &idle_task;
  current_kstack = idle_task.kstack_top;
}
static struct task *group_leader_locked(struct task *t) {
  struct task *x = t;
  do {
    if (x->pid == t->tgid)
      return x;
    x = x->next;
  } while (x != t);
  return t;
}

static struct ksigaction *sigact_of(struct task *t, int sig) {
  struct ksigaction *tab = sigtab[t->tgid];
  return tab ? &tab[sig] : &sig_dummy[sig];
}

static int create_task(void (*fn)(void *), void *arg, const char *name, int same_group) {
  struct task *t = kmalloc(sizeof *t);
  if (!t)
    return -1;
  memset(t, 0, sizeof *t);
  t->stack = kmalloc(TASK_STACK);
  if (!t->stack) {
    kfree(t);
    return -1;
  }
  struct ksigaction *tab = 0;
  if (!same_group) {
    tab = kmalloc(sizeof(struct ksigaction) * 64);
    if (!tab) {
      kfree(t->stack);
      kfree(t);
      return -1;
    }
    memset(tab, 0, sizeof(struct ksigaction) * 64);
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
  t->mmap_base = MMAP_BASE;
  t->fsbase = current ? current->fsbase : 0;
  memcpy(fpu_area(t), fpu_tmpl(), 512);
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
    if (tab)
      kfree(tab);
    kfree(t->stack);
    kfree(t);
    return -11;
  }
  t->pid = pid;
  if (same_group) {
    t->tgid = current->tgid;
    t->parent = 0;
    t->sig_mask = current->sig_mask;
    group_nthreads[t->tgid]++;
  } else {
    t->tgid = pid;
    t->parent = current ? group_leader_locked(current) : 0;
    group_nthreads[pid] = 1;
    group_exiting[pid] = 0;
    sigtab[pid] = tab;
  }
  t->next = current->next;
  current->next = t;
  spin_unlock_irqrestore(&sched_lock, flags);
  return pid;
}

int kthread_create(void (*fn)(void *), void *arg, const char *name) {
  return create_task(fn, arg, name, 0);
}

int thread_create(void (*fn)(void *), void *arg, const char *name) {
  return create_task(fn, arg, name, 1);
}

void task_fpu_inherit(int pid) {
  uint64_t flags;
  spin_lock_irqsave(&sched_lock, &flags);
  struct task *t = current->next, *start = t;
  do {
    if ((int)t->pid == pid) {
      fpu_save(current);
      memcpy(fpu_area(t), fpu_area(current), 512);
      break;
    }
    t = t->next;
  } while (t != start);
  spin_unlock_irqrestore(&sched_lock, flags);
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
    fpu_save(p);
    fpu_load(best);
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
  fpu_load(nx);
  spin_unlock(&sched_lock);
  context_switch(dead, nx);
  for (;;)
    ;
}
#endif

static void kill_others_locked(struct task *me, int quiet) {
  struct task *t = me->next;
  while (t != me) {
    if (t->tgid == me->tgid && t->state != T_DEAD) {
      if (quiet)
        t->kill_quiet = 1;
      __atomic_fetch_or(&t->sig_pending, 1ULL << SIGKILL, __ATOMIC_ACQ_REL);
      if (t->state == T_BLOCKED) {
        t->futex_addr = 0;
        t->sleep_until = 0;
        t->state = T_READY;
      }
    }
    t = t->next;
  }
}

static void wake_waiters_locked(struct task *p) {
  struct task *t = p->next;
  do {
    if (t->tgid == p->tgid && t->waiting_child && t->state == T_BLOCKED)
      t->state = T_READY;
    t = t->next;
  } while (t != p->next);
}

static int signal_ignored_by_default(int sig) {
  return sig == SIGCHLD || sig == SIGURG || sig == SIGWINCH || sig == SIGCONT;
}

static void group_post_locked(struct task *any, int sig) {
  struct task *pick = 0, *alive = 0;
  uint64_t bit = 1ULL << sig;
  struct task *t = any;
  do {
    if (t->tgid == any->tgid && t->state != T_DEAD) {
      if (!alive)
        alive = t;
      if (!pick && (sig == SIGKILL || sig == SIGSTOP || !(t->sig_mask & bit)))
        pick = t;
    }
    t = t->next;
  } while (t != any);
  if (!pick)
    pick = alive ? alive : any;
  __atomic_fetch_or(&pick->sig_pending, bit, __ATOMIC_ACQ_REL);
  if (pick->state == T_BLOCKED) {
    pick->futex_addr = 0;
    pick->state = T_READY;
  }
}

void proc_user_write(uint64_t addr, const void *src, uint64_t n) {
  if (!n || addr < 0x10000 || addr + n > USER_LIM || addr + n < addr)
    return;
  for (uint64_t pg = addr & ~0xFFFULL; pg < addr + n; pg += 4096)
    vm_cow_handle((uint64_t *)current->cr3, pg);
  memcpy((void *)addr, src, n);
}

static void exit_task(int status, int whole_group) {
  struct task *me = current;
  uint32_t tg = me->tgid;
  if (me->clear_child_tid) {
    uint32_t zero = 0;
    uint64_t addr = me->clear_child_tid;
    me->clear_child_tid = 0;
    proc_user_write(addr, &zero, 4);
    futex_wake_key(me->cr3, addr, 1, 0xFFFFFFFFu);
  }
  uint64_t flags;
  spin_lock_irqsave(&sched_lock, &flags);
  struct task *ldr = group_leader_locked(me);
  if (whole_group) {
    if (!group_exiting[tg]) {
      group_exiting[tg] = 1;
      ldr->exit_code = status;
    }
    kill_others_locked(me, 0);
  } else if (me == ldr && !group_exiting[tg]) {
    ldr->exit_code = status;
  }
  int last = (--group_nthreads[tg] == 0);
  spin_unlock_irqrestore(&sched_lock, flags);

  if (last) {
    cleanup_process_fds(tg);
    release_mm();
  } else {
    me->cr3 = 0;
  }

  spin_lock_irqsave(&sched_lock, &flags);
  me->state = T_DEAD;
  if (last) {
    ldr->cr3 = 0;
    orphan_children_locked(ldr);
    if (ldr->parent) {
      group_post_locked(ldr->parent, SIGCHLD);
      wake_waiters_locked(ldr->parent);
    }
  }
  spin_unlock(&sched_lock);

  for (;;) {
    yield();
  }
}

void do_exit(int code) {
  exit_task((code & 0xff) << 8, 0);
}

void do_exit_group(int code) {
  exit_task((code & 0xff) << 8, 1);
}

int wait4(int pid, int *status) {
  uint64_t flags;
  spin_lock_irqsave(&sched_lock, &flags);
  for (;;) {
    int any = 0;
    struct task *me = group_leader_locked(current);
    struct task *t = current->next, *start = t;
    do {
      if (t->parent == me && (pid == -1 || pid == (int)t->pid)) {
        any = 1;
        if (t->state == T_DEAD && group_nthreads[t->tgid] == 0) {
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
          free_sigtab(rpid);
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
    current->waiting_child = 1;
    current->state = T_BLOCKED;
    spin_unlock(&sched_lock);
    schedule();
    spin_lock(&sched_lock);
    current->waiting_child = 0;
  }
}

int proc_getppid(void) {
  uint64_t flags;
  spin_lock_irqsave(&sched_lock, &flags);
  struct task *l = group_leader_locked(current);
  int r = l->parent ? (int)l->parent->pid : 0;
  spin_unlock_irqrestore(&sched_lock, flags);
  return r;
}

static int futex_ts_ticks(uint64_t tsp, int absolute, uint64_t *ticks) {
  if (!tsp) {
    *ticks = 0;
    return 0;
  }
  if (tsp < 0x10000 || tsp + 16 > USER_LIM)
    return -14;
  int64_t sec = ((int64_t *)tsp)[0];
  int64_t nsec = ((int64_t *)tsp)[1];
  if (sec < 0 || nsec < 0 || nsec >= 1000000000L)
    return -22;
  uint64_t ns = (uint64_t)sec * 1000000000ULL + (uint64_t)nsec;
  if (absolute) {
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    uint64_t now = ((uint64_t)hi << 32) | lo;
    ns = ns > now ? ns - now : 0;
    if (ns == 0)
      return -110;
  }
  uint64_t tk = (ns + 9999999) / 10000000;
  *ticks = tk ? tk : 1;
  return 0;
}

int futex_wake_key(uint64_t cr3, uint64_t addr, int n, uint32_t bitset) {
  uint64_t flags;
  spin_lock_irqsave(&sched_lock, &flags);
  int woken = 0;
  struct task *t = current->next, *start = t;
  do {
    if (woken < n && t->state == T_BLOCKED && t->futex_addr == addr &&
        t->futex_cr3 == cr3 && (t->futex_bitset & bitset)) {
      t->state = T_READY;
      t->futex_addr = 0;
      t->sleep_until = 0;
      woken++;
    }
    t = t->next;
  } while (t != start);
  spin_unlock_irqrestore(&sched_lock, flags);
  return woken;
}

static int futex_wait_ex(uint32_t *u, uint32_t val, uint64_t ticks, uint32_t bitset) {
  uint64_t addr = (uint64_t)u;
  volatile uint32_t probe = *(volatile uint32_t *)u;
  (void)probe;
  uint64_t flags;
  spin_lock_irqsave(&sched_lock, &flags);
  if (*(volatile uint32_t *)u != val) {
    spin_unlock_irqrestore(&sched_lock, flags);
    return -11;
  }
  if (signal_interrupt()) {
    spin_unlock_irqrestore(&sched_lock, flags);
    return -4;
  }
  uint64_t deadline = ticks ? timer_ticks + ticks : 0;
  current->futex_addr = addr;
  current->futex_cr3 = current->cr3;
  current->futex_bitset = bitset;
  current->sleep_until = deadline;
  current->state = T_BLOCKED;
  spin_unlock(&sched_lock);
  schedule();
  irq_restore(flags);
  int r = 0;
  spin_lock_irqsave(&sched_lock, &flags);
  if (current->futex_addr != 0) {
    current->futex_addr = 0;
    if (signal_interrupt())
      r = -4;
    else if (deadline && timer_ticks >= deadline)
      r = -110;
  }
  current->sleep_until = 0;
  spin_unlock_irqrestore(&sched_lock, flags);
  return r;
}

int futex_op(uint32_t *uaddr, int op, uint32_t val, uint64_t timeout, uint32_t *uaddr2, uint32_t val3) {
  int cmd = op & 127;
  uint64_t addr = (uint64_t)uaddr;
  if (addr < 0x10000 || addr + 4 > USER_LIM)
    return -14;
  if (addr & 3)
    return -22;
  uint64_t ticks;
  int rc;
  switch (cmd) {
  case 0:
    rc = futex_ts_ticks(timeout, 0, &ticks);
    if (rc)
      return rc;
    return futex_wait_ex(uaddr, val, ticks, 0xFFFFFFFFu);
  case 9:
    if (!val3)
      return -22;
    rc = futex_ts_ticks(timeout, 1, &ticks);
    if (rc)
      return rc;
    return futex_wait_ex(uaddr, val, ticks, val3);
  case 1:
    return futex_wake_key(current->cr3, addr, (int)val, 0xFFFFFFFFu);
  case 10:
    if (!val3)
      return -22;
    return futex_wake_key(current->cr3, addr, (int)val, val3);
  case 3:
  case 4: {
    uint64_t addr2 = (uint64_t)uaddr2;
    if (addr2 < 0x10000 || addr2 + 4 > USER_LIM)
      return -14;
    if (addr2 & 3)
      return -22;
    int max_requeue = (int)timeout;
    volatile uint32_t probe = *(volatile uint32_t *)uaddr;
    (void)probe;
    uint64_t flags;
    spin_lock_irqsave(&sched_lock, &flags);
    if (cmd == 4 && *(volatile uint32_t *)uaddr != val3) {
      spin_unlock_irqrestore(&sched_lock, flags);
      return -11;
    }
    int woken = 0, moved = 0;
    int wake_max = (int)val;
    struct task *t = current->next, *start = t;
    do {
      if (t->state == T_BLOCKED && t->futex_addr == addr && t->futex_cr3 == current->cr3) {
        if (woken < wake_max) {
          t->state = T_READY;
          t->futex_addr = 0;
          t->sleep_until = 0;
          woken++;
        } else if (moved < max_requeue) {
          t->futex_addr = addr2;
          moved++;
        }
      }
      t = t->next;
    } while (t != start);
    spin_unlock_irqrestore(&sched_lock, flags);
    return woken + moved;
  }
  default:
    return -38;
  }
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

struct uregs {
  uint64_t rax, rbx, rcx, rdx, rsi, rdi, rbp;
  uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
  uint64_t rip, rsp, rflags;
};

struct ksigcontext {
  uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
  uint64_t rdi, rsi, rbp, rbx, rdx, rax, rcx, rsp, rip, eflags;
  uint16_t cs, gs, fs, ss;
  uint64_t err, trapno, oldmask, cr2;
  uint64_t fpstate;
  uint64_t reserved1[8];
};

struct kucontext {
  uint64_t uc_flags;
  uint64_t uc_link;
  uint64_t ss_sp;
  int32_t ss_flags;
  int32_t ss_pad;
  uint64_t ss_size;
  struct ksigcontext mc;
  uint64_t sigmask;
};

struct ksiginfo {
  int32_t si_signo, si_errno, si_code, si_pad;
  uint64_t si_a, si_b;
  uint8_t pad[128 - 32];
};

struct rt_sigframe {
  uint64_t pretcode;
  struct kucontext uc;
  struct ksiginfo info;
};

extern volatile uint8_t force_iret;

static void die_by_signal(int sig) {
  exit_task(sig & 0x7f, 1);
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
      if ((int)t->pid == pid && t->state != T_DEAD) {
        group_post_locked(t, sig);
        break;
      }
      t = t->next;
    } while (t != start);
  }
  spin_unlock_irqrestore(&sched_lock, flags);
}

int proc_group_post(int tgid, int sig) {
  uint64_t flags;
  int ret = -3;
  spin_lock_irqsave(&sched_lock, &flags);
  struct task *t = current->next, *start = t;
  do {
    if ((int)t->pid == tgid && t->tgid == t->pid) {
      if (sig > 0)
        group_post_locked(t, sig);
      ret = 0;
      break;
    }
    t = t->next;
  } while (t != start);
  spin_unlock_irqrestore(&sched_lock, flags);
  return ret;
}

int proc_kill(int pid, int sig) {
  if (pid > 0)
    return proc_group_post(pid, sig);
  if (pid != 0 && pid != -1)
    return -22;
  uint64_t flags;
  spin_lock_irqsave(&sched_lock, &flags);
  struct task *t = current->next, *start = t;
  do {
    if (t->tgid == t->pid && t->state != T_DEAD && (pid == -1 || t->pgrp == current->pgrp)) {
      if (sig > 0)
        group_post_locked(t, sig);
    }
    t = t->next;
  } while (t != start);
  spin_unlock_irqrestore(&sched_lock, flags);
  return 0;
}

int proc_tgkill(int tgid, int tid, int sig) {
  uint64_t flags;
  int ret = -3;
  spin_lock_irqsave(&sched_lock, &flags);
  struct task *t = current->next, *start = t;
  do {
    if ((int)t->pid == tid && t->state != T_DEAD && (tgid <= 0 || (int)t->tgid == tgid)) {
      if (sig > 0) {
        __atomic_fetch_or(&t->sig_pending, 1ULL << sig, __ATOMIC_ACQ_REL);
        if (t->state == T_BLOCKED) {
          t->futex_addr = 0;
          t->state = T_READY;
        }
      }
      ret = 0;
      break;
    }
    t = t->next;
  } while (t != start);
  spin_unlock_irqrestore(&sched_lock, flags);
  return ret;
}

struct ksigaction *sigact_slot(int sig) {
  return sigact_of(current, sig);
}

void proc_inherit_signals(int child_pid) {
  uint64_t flags;
  spin_lock_irqsave(&sched_lock, &flags);
  struct task *t = current->next, *start = t;
  do {
    if ((int)t->pid == child_pid) {
      struct ksigaction *dst = sigtab[child_pid];
      if (dst)
        memcpy(dst, sigact_of(current, 0), sizeof(struct ksigaction) * 64);
      t->sig_mask = current->sig_mask;
      break;
    }
    t = t->next;
  } while (t != start);
  spin_unlock_irqrestore(&sched_lock, flags);
}

void proc_thread_started(uint64_t ctid, int set_tid, int clear_tid) {
  if (set_tid && ctid) {
    uint32_t tid = current->pid;
    proc_user_write(ctid, &tid, 4);
  }
  if (clear_tid)
    current->clear_child_tid = ctid;
}

void proc_exec_prepare(void) {
  uint64_t flags;
  spin_lock_irqsave(&sched_lock, &flags);
  if (group_nthreads[current->tgid] > 1)
    kill_others_locked(current, 1);
  spin_unlock_irqrestore(&sched_lock, flags);
  for (;;) {
    spin_lock_irqsave(&sched_lock, &flags);
    int n = group_nthreads[current->tgid];
    spin_unlock_irqrestore(&sched_lock, flags);
    if (n <= 1)
      break;
    yield();
  }
  struct ksigaction *tab = sigtab[current->tgid];
  if (tab) {
    for (int i = 1; i < 64; i++) {
      if (tab[i].handler != SIG_DFL && tab[i].handler != SIG_IGN) {
        tab[i].handler = SIG_DFL;
        tab[i].flags = 0;
        tab[i].restorer = 0;
        tab[i].mask = 0;
      }
    }
  }
  current->alt_sp = 0;
  current->alt_size = 0;
  current->alt_flags = 2;
  current->clear_child_tid = 0;
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

static int setup_frame(struct uregs *u, int sig, struct ksigaction *sa) {
  if (!(sa->flags & SA_RESTORER) || sa->handler < 0x10000 || sa->handler >= USER_LIM)
    die_by_signal(SIGSEGV);
  uint64_t sp = u->rsp;
  if ((sa->flags & SA_ONSTACK) && current->alt_size && !(current->alt_flags & 2)) {
    if (!(sp >= current->alt_sp && sp < current->alt_sp + current->alt_size))
      sp = current->alt_sp + current->alt_size;
  }
  if (sp < 0x10000 + 128 + 1024 || sp > USER_LIM)
    die_by_signal(SIGSEGV);
  sp -= 128;
  uint64_t fp = (sp - 512) & ~0xFULL;
  uint64_t fr = ((fp - sizeof(struct rt_sigframe)) & ~0xFULL) - 8;
  if (fr < 0x10000 || fp + 512 > USER_LIM)
    die_by_signal(SIGSEGV);

  struct rt_sigframe f;
  memset(&f, 0, sizeof f);
  f.pretcode = sa->restorer;
  f.uc.ss_sp = current->alt_sp;
  f.uc.ss_flags = (int32_t)current->alt_flags;
  f.uc.ss_size = current->alt_size;
  f.uc.mc.r8 = u->r8;
  f.uc.mc.r9 = u->r9;
  f.uc.mc.r10 = u->r10;
  f.uc.mc.r11 = u->r11;
  f.uc.mc.r12 = u->r12;
  f.uc.mc.r13 = u->r13;
  f.uc.mc.r14 = u->r14;
  f.uc.mc.r15 = u->r15;
  f.uc.mc.rdi = u->rdi;
  f.uc.mc.rsi = u->rsi;
  f.uc.mc.rbp = u->rbp;
  f.uc.mc.rbx = u->rbx;
  f.uc.mc.rdx = u->rdx;
  f.uc.mc.rax = u->rax;
  f.uc.mc.rcx = u->rcx;
  f.uc.mc.rsp = u->rsp;
  f.uc.mc.rip = u->rip;
  f.uc.mc.eflags = u->rflags;
  f.uc.mc.cs = 0x2b;
  f.uc.mc.ss = 0x23;
  f.uc.mc.fpstate = fp;
  f.uc.sigmask = current->sig_mask >> 1;
  f.info.si_signo = sig;

  uint8_t *fx = fx_scratch(current);
  __asm__ volatile("fxsave (%0)" ::"r"(fx) : "memory");
  proc_user_write(fr, &f, sizeof f);
  proc_user_write(fp, fx, 512);
  __asm__ volatile("fxrstor (%0)" ::"r"(fpu_tmpl()) : "memory");

  u->rsp = fr;
  u->rip = sa->handler;
  u->rdi = (uint64_t)sig;
  u->rsi = fr + (uint64_t)__builtin_offsetof(struct rt_sigframe, info);
  u->rdx = fr + (uint64_t)__builtin_offsetof(struct rt_sigframe, uc);
  u->rax = 0;
  u->rflags &= ~(0x400ULL | 0x100ULL);

  uint64_t unmaskable = (1ULL << SIGKILL) | (1ULL << SIGSTOP);
  current->sig_mask |= sa->mask;
  if (!(sa->flags & SA_NODEFER))
    current->sig_mask |= 1ULL << sig;
  current->sig_mask &= ~unmaskable;
  if (sa->flags & SA_RESETHAND) {
    sa->handler = SIG_DFL;
    sa->flags &= ~SA_SIGINFO;
  }
  return 1;
}

static int deliver_core(struct uregs *u) {
  for (int sig = 1; sig < 64; sig++) {
    uint64_t bit = 1ULL << sig;
    if (!(current->sig_pending & bit))
      continue;
    int force = (sig == SIGKILL || sig == SIGSTOP);
    if (!force && (current->sig_mask & bit))
      continue;
    __atomic_fetch_and(&current->sig_pending, ~bit, __ATOMIC_ACQ_REL);
    struct ksigaction *sa = sigact_of(current, sig);
    uint64_t handler = force ? SIG_DFL : sa->handler;
    if (handler == SIG_IGN)
      continue;
    if (handler == SIG_DFL) {
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
        if (sig == SIGKILL && current->kill_quiet)
          exit_task(0, 0);
        die_by_signal(sig);
        return 0;
      }
    }
    return setup_frame(u, sig, sa);
  }
  return 0;
}

int signal_interrupt(void) {
  if (!current)
    return 0;
  uint64_t unmaskable = (1ULL << SIGKILL) | (1ULL << SIGSTOP);
  uint64_t pend = current->sig_pending & ~(current->sig_mask & ~unmaskable);
  for (int sig = 1; sig < 64 && pend; sig++) {
    uint64_t bit = 1ULL << sig;
    if (!(pend & bit))
      continue;
    pend &= ~bit;
    if (sig == SIGKILL || sig == SIGSTOP)
      return 1;
    uint64_t h = sigact_of(current, sig)->handler;
    if (h == SIG_IGN)
      continue;
    if (h == SIG_DFL && signal_ignored_by_default(sig))
      continue;
    return 1;
  }
  return 0;
}

void proc_deliver_regs(void *regs) {
  struct pt_regs *r = regs;
  if (!current || !current->sig_pending)
    return;
  struct uregs u;
  u.rax = r->rax; u.rbx = r->rbx; u.rcx = r->rcx; u.rdx = r->rdx;
  u.rsi = r->rsi; u.rdi = r->rdi; u.rbp = r->rbp;
  u.r8 = r->r8; u.r9 = r->r9; u.r10 = r->r10; u.r11 = r->r11;
  u.r12 = r->r12; u.r13 = r->r13; u.r14 = r->r14; u.r15 = r->r15;
  u.rip = r->saved_rip; u.rsp = r->saved_rsp; u.rflags = r->saved_rflags;
  if (!deliver_core(&u))
    return;
  r->rax = u.rax; r->rdi = u.rdi; r->rsi = u.rsi; r->rdx = u.rdx;
  r->saved_rip = u.rip; r->saved_rsp = u.rsp; r->saved_rflags = u.rflags;
}

void deliver_signals(struct int_frame *f) {
  if (!current || !current->sig_pending)
    return;
  if ((f->cs & 3) != 3)
    return;
  struct uregs u;
  u.rax = f->rax; u.rbx = f->rbx; u.rcx = f->rcx; u.rdx = f->rdx;
  u.rsi = f->rsi; u.rdi = f->rdi; u.rbp = f->rbp;
  u.r8 = f->r8; u.r9 = f->r9; u.r10 = f->r10; u.r11 = f->r11;
  u.r12 = f->r12; u.r13 = f->r13; u.r14 = f->r14; u.r15 = f->r15;
  u.rip = f->rip; u.rsp = f->rsp; u.rflags = f->rflags;
  if (!deliver_core(&u))
    return;
  f->rax = u.rax; f->rdi = u.rdi; f->rsi = u.rsi; f->rdx = u.rdx;
  f->rip = u.rip; f->rsp = u.rsp; f->rflags = u.rflags;
}

int64_t proc_sigreturn(void *regs) {
  struct pt_regs *r = regs;
  uint64_t fr = r->saved_rsp - 8;
  if (fr < 0x10000 || fr + sizeof(struct rt_sigframe) > USER_LIM)
    die_by_signal(SIGSEGV);
  struct rt_sigframe f;
  memcpy(&f, (void *)fr, sizeof f);
  struct ksigcontext *m = &f.uc.mc;
  if (m->rip < 0x10000 || m->rip >= USER_LIM || m->rsp < 0x10000 || m->rsp >= USER_LIM)
    die_by_signal(SIGSEGV);
  r->rbx = m->rbx; r->rcx = m->rcx; r->rdx = m->rdx; r->rsi = m->rsi;
  r->rdi = m->rdi; r->rbp = m->rbp;
  r->r8 = m->r8; r->r9 = m->r9; r->r10 = m->r10; r->r11 = m->r11;
  r->r12 = m->r12; r->r13 = m->r13; r->r14 = m->r14; r->r15 = m->r15;
  r->saved_rip = m->rip;
  r->saved_rsp = m->rsp;
  r->saved_rflags = (m->eflags & 0xCD5ULL) | 0x202ULL;
  current->sig_mask = (f.uc.sigmask << 1) & ~((1ULL << SIGKILL) | (1ULL << SIGSTOP));
  uint64_t fp = m->fpstate;
  if (fp >= 0x10000 && fp + 512 <= USER_LIM) {
    uint8_t *fx = fx_scratch(current);
    memcpy(fx, (void *)fp, 512);
    uint32_t mxcsr = *(uint32_t *)(fx + 24);
    uint32_t mask = *(uint32_t *)(fx + 28);
    if (!mask)
      mask = 0xFFBF;
    *(uint32_t *)(fx + 24) = mxcsr & mask;
    __asm__ volatile("fxrstor (%0)" ::"r"(fx) : "memory");
  }
  force_iret = 1;
  return (int64_t)m->rax;
}
