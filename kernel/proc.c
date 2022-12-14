#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

// from FreeBSD.
int
do_rand(unsigned long *ctx)
{
/*
 * Compute x = (7^5 * x) mod (2^31 - 1)
 * without overflowing 31 bits:
 *      (2^31 - 1) = 127773 * (7^5) + 2836
 * From "Random number generators: good ones are hard to find",
 * Park and Miller, Communications of the ACM, vol. 31, no. 10,
 * October 1988, p. 1195.
 */
    long hi, lo, x;

    /* Transform to [1, 0x7ffffffe] range. */
    x = (*ctx % 0x7ffffffe) + 1;
    hi = x / 127773;
    lo = x % 127773;
    x = 16807 * lo - 2836 * hi;
    if (x < 0)
        x += 0x7fffffff;
    /* Transform to [0, 0x7ffffffd] range. */
    x--;
    *ctx = x;
    return (x);
}

unsigned long rand_next = 1;

int
rand(void)
{
    return (do_rand(&rand_next));
}

struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);

int totaltickets = 0;

extern char trampoline[]; // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void
proc_mapstacks(pagetable_t kpgtbl)
{
  struct proc *p;
  
  for(p = proc; p < &proc[NPROC]; p++) {
    char *pa = kalloc();
    if(pa == 0)
      panic("kalloc");
    uint64 va = KSTACK((int) (p - proc));
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  }
}

// initialize the proc table.
void
procinit(void)
{
  struct proc *p;
  
  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  for(p = proc; p < &proc[NPROC]; p++) {
      initlock(&p->lock, "proc");
      p->state = UNUSED;
      p->kstack = KSTACK((int) (p - proc));
  }
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int
cpuid()
{
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu*
mycpu(void)
{
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc*
myproc(void)
{
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

int
allocpid()
{
  int pid;
  
  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state == UNUSED) {
      goto found;
    } else {
      release(&p->lock);
    }
  }
  return 0;

found:
  p->pid = allocpid();
  p->state = USED;
  p->trace = 0;
  p->tracemask = 0;
#if defined(FCFS)
  p->stick = ticks; // from defs.h, set by clock_intr
#elif defined(PBS)
  p->stick = ticks; // from defs.h, set by clock_intr
  p->priority = 60; // default static priority is 60
  p->tickls = 0;
  p->tickrng = 0;
  p->tickslp = 0;
  p->niceness = 5;  // default niceness is 5
  p->nscheduled = 0;
#endif

#if defined(LBS)
  p->tickets = 0; // by default one ticket assigned to process
#endif

#if defined(MLFQ)
  p->queue = 0;
  p->waittime = 0;
  p->ticksused = 0;
  p->intime = 0;
#if defined(TRACE_QUEUE)
      printf("[%d] started process %d\n", ticks, p->pid);
#endif
#endif

  // Allocate a trapframe page.
  if((p->trapframe = (struct trapframe *)kalloc()) == 0 || (p->trapcopy = (struct trapframe *)kalloc()) == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  p->sigalarm = 0;
  p->ticksn = 0;
  p->ticksp = 0;
  p->tickspa = 0;
  p->handler = 0;

  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;
  p->rtime = 0;
  p->etime = 0;
  p->ctime = ticks;
  return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
  if(p->trapframe)
    kfree((void*)p->trapframe);
  p->trapframe = 0;
  if(p->trapcopy)
    kfree((void*)p->trapcopy);
  p->trapcopy = 0;
  if(p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
#if defined(LBS)
  p->tickets = 0;
#endif
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->state = UNUSED;
  p->trace = 0;
  p->sigalarm = 0;
  p->ticksn = 0;
  p->ticksp = 0;
  p->tickspa = 0;
  p->handler = 0;
#if defined(FCFS)
  p->stick = 0;
#elif defined(PBS)
  p->stick = 0;
  p->tickls = 0;
  p->tickslp = 0;
  p->tickrng = 0;
  p->priority = 0;
  p->niceness = 0;
  p->nscheduled = 0;
#endif

#if defined(MLFQ)
  p->queue = 0;
  p->waittime = 0;
  p->ticksused = 0;
  p->intime = 0;
#endif
}

// Create a user page table for a given process, with no user memory,
// but with trampoline and trapframe pages.
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if(pagetable == 0)
    return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if(mappages(pagetable, TRAMPOLINE, PGSIZE,
              (uint64)trampoline, PTE_R | PTE_X) < 0){
    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe page just below the trampoline page, for
  // trampoline.S.
  if(mappages(pagetable, TRAPFRAME, PGSIZE,
              (uint64)(p->trapframe), PTE_R | PTE_W) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// a user program that calls exec("/init")
// assembled from ../user/initcode.S
// od -t xC ../user/initcode
uchar initcode[] = {
  0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02,
  0x97, 0x05, 0x00, 0x00, 0x93, 0x85, 0x35, 0x02,
  0x93, 0x08, 0x70, 0x00, 0x73, 0x00, 0x00, 0x00,
  0x93, 0x08, 0x20, 0x00, 0x73, 0x00, 0x00, 0x00,
  0xef, 0xf0, 0x9f, 0xff, 0x2f, 0x69, 0x6e, 0x69,
  0x74, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00
};

// Set up first user process.
void
userinit(void)
{
  struct proc *p;

  p = allocproc();
  initproc = p;
  
  // allocate one user page and copy initcode's instructions
  // and data into it.
  uvmfirst(p->pagetable, initcode, sizeof(initcode));
  p->sz = PGSIZE;

  // prepare for the very first "return" from kernel to user.
  p->trapframe->epc = 0;      // user program counter
  p->trapframe->sp = PGSIZE;  // user stack pointer

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  p->state = RUNNABLE;

#if defined(MLFQ)
  p->queue = 0;
  p->intime = ticks;
#endif

#if defined(LBS)
  p->tickets = 1;
  totaltickets++;
#endif

  release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint64 sz;
  struct proc *p = myproc();

  sz = p->sz;
  if(n > 0){
    if((sz = uvmalloc(p->pagetable, sz, sz + n, PTE_W)) == 0) {
      return -1;
    }
  } else if(n < 0){
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    printf("allocproc fail\n");
    return -1;
  }

  // Copy user memory from parent to child.
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    release(&np->lock);
    printf("uvmcopy fail\n");
    return -1;
  }
  np->sz = p->sz;

  // trace a fork if parent is also traced
  np->trace = p->trace;
  np->tracemask = p->tracemask;

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;

#if defined(MLFQ)
  p->queue = 0;
  p->intime = ticks;
#endif

#if defined(LBS)
  // child should have same no. of tickets as parent
  np->tickets = p->tickets;
  totaltickets += np->tickets;
#endif
  release(&np->lock);

  return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void
reparent(struct proc *p)
{
  struct proc *pp;

  for(pp = proc; pp < &proc[NPROC]; pp++){
    if(pp->parent == p){
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void
exit(int status)
{
  struct proc *p = myproc();

  if(p == initproc)
    panic("init exiting");

  // Close all open files.
  for(int fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd]){
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  acquire(&wait_lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup(p->parent);
  
  acquire(&p->lock);

  p->xstate = status;
  p->state = ZOMBIE;
  p->etime = ticks;

  release(&wait_lock);

#if defined(TRACE_QUEUE)
      printf("[%d] exited process %d\n", ticks, p->pid);
#endif

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(uint64 addr)
{
  struct proc *pp;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(pp = proc; pp < &proc[NPROC]; pp++){
      if(pp->parent == p){
        // make sure the child isn't still in exit() or swtch().
        acquire(&pp->lock);

        havekids = 1;
        if(pp->state == ZOMBIE){
          // Found one.
          pid = pp->pid;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&pp->xstate,
                                  sizeof(pp->xstate)) < 0) {
            release(&pp->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(pp);
          release(&pp->lock);
          release(&wait_lock);
          return pid;
        }
        release(&pp->lock);
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || killed(p)){
      release(&wait_lock);
      return -1;
    }
    
    // Wait for a child to exit.
    sleep(p, &wait_lock);  //DOC: wait-sleep
  }
}

#if defined(PBS)
int compute_priority(int p, int n)
{
  int r = p - n + 5;
  if(r > 100) r = 100;
  if(r < 0) r = 0;
  return r;
}

// returns non-zero if p should be scheduled before best
int compare_priority(struct proc* best, struct proc* p)
{
  int p1 = compute_priority(best->priority, best->niceness);
  int p2 = compute_priority(p->priority, p->niceness);
  if(p1 != p2) return p2 < p1;
  if(best->nscheduled != p->nscheduled) return p->nscheduled < best->nscheduled;
  return p->stick > best->stick;
}

int set_priority(int new_priority, int pid)
{
  struct proc* p;
  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if((p->state == SLEEPING || p->state == RUNNING || p->state == RUNNABLE) && p->pid == pid) {
      p->niceness = 5;
      int old = p->priority;
      p->priority = new_priority;
      release(&p->lock);
      if(new_priority < old) yield();
      return old;
    }
    release(&p->lock);
  }
  return -1;
}
#endif

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  
  c->proc = 0;

#if defined(FCFS) || defined(PBS)
  struct proc *best = 0;
  for(;;){
    // Avoid deadlock by ensuring that devices can interrupt.
    intr_on();

    for(p = proc; p < &proc[NPROC]; p++) {
      acquire(&p->lock);
      if(p->state == RUNNABLE) {
#if defined(FCFS)
        if(best == 0) best = p;
        else if(best->stick > p->stick) {
          release(&best->lock);
          best = p;
        }
#elif defined(PBS)
        if(best == 0) best = p;
        else if(compare_priority(best, p)) {
          release(&best->lock);
          best = p;
        }
#endif
      }
      if(best != p) release(&p->lock);
    }

    if(best != 0) {
      best->state = RUNNING;
      c->proc = best;
#if defined(PBS)
      // acquire(&tickslock);
      best->tickls = ticks;
      // release(&tickslock);
      best->nscheduled++;
#endif
      swtch(&c->context, &best->context);
      c->proc = 0;
      release(&best->lock);
    }
    best = 0;
  }
#elif defined(MLFQ)
  for(;;) {
    // Avoid deadlock by ensuring that devices can interrupt.
    intr_on();
    struct proc* best = 0;
    for(p = proc; p < &proc[NPROC]; p++) {
      acquire(&p->lock);
      if(p->state == RUNNABLE) {
        if(best == 0) {
          best = p;
          continue;
        }

        if((best->queue == p->queue && p->intime < best->intime) || (p->queue < best->queue)) {
          release(&best->lock);
          best = p;
          continue;
        }
      }
      release(&p->lock);
    }
    if(best != 0) {
      best->ticksused = 0;
      best->waittime = 0;

      best->state = RUNNING;
      c->proc = best;
      // printf("scheduling %s with pid %d now\n", best->name, best->pid);
      swtch(&c->context, &best->context);
      c->proc = 0;
      release(&best->lock);
    }
    best = 0;
  }
#elif defined(LBS)
  for(;;){
    // Avoid deadlock by ensuring that devices can interrupt.
    intr_on();

    static int ctickets = 0;  // to store cumulative ticket sum of runnable processes
    int i = 0;
    int pflag = 0;
    int rn = (rand() % totaltickets) + 1;
    for(p = proc; p < &proc[NPROC]; p++) {
      acquire(&p->lock);
      if(p->state == RUNNABLE) {
        // printf("rn = %d\ntotal tickets = %d\n", rn, totaltickets);
        if(i == 0)
        {
          ctickets = p->tickets;
        }
        else
        {
          ctickets = p->tickets + ctickets;
        }
        if(ctickets >= rn && pflag == 0)
        {
          // printf("ctickets = %d\n", ctickets);
          pflag = 1;
          // Switch to chosen process.  It is the process's job
          // to release its lock and then reacquire it
          // before jumping back to us.
          p->state = RUNNING;
          totaltickets -= p->tickets;
          c->proc = p;
          swtch(&c->context, &p->context);

          // Process is done running for now.
          // It should have changed its p->state before coming back.
          c->proc = 0;
          release(&p->lock);
          break;
        }
        i++;
      }
      release(&p->lock);
    }
  }
#else
  for(;;){
    // Avoid deadlock by ensuring that devices can interrupt.
    intr_on();

    for(p = proc; p < &proc[NPROC]; p++) {
      acquire(&p->lock);
      if(p->state == RUNNABLE) {
        // Switch to chosen process.  It is the process's job
        // to release its lock and then reacquire it
        // before jumping back to us.
        p->state = RUNNING;
        c->proc = p;
        swtch(&c->context, &p->context);

        // Process is done running for now.
        // It should have changed its p->state before coming back.
        c->proc = 0;
      }
      release(&p->lock);
    }
  }
#endif
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&p->lock))
    panic("sched p->lock");
  if(mycpu()->noff != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

#if !defined(FCFS)
// Give up the CPU for one scheduling round.
// will never be called in FCFS scheduling
void
yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);

#if defined(LBS)
    totaltickets += p->tickets;
#endif

  p->state = RUNNABLE;
#if defined(MLFQ)
  // move to next queue
  if(p->queue != NQUEUE - 1 && p->ticksused >= (1 << p->queue)) {
#if defined(TRACE_QUEUE)
    printf("[%d] queue for %d changed from %d to %d\n", ticks, p->pid, p->queue, p->queue + 1);
#endif
    p->queue++;
  }

  p->intime = ticks;
  p->ticksused = 0;
#endif

#if defined(PBS)
  p->tickrng = ticks;
#endif

  sched();
  release(&p->lock);
}
#endif

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void
forkret(void)
{
  static int first = 1;

  // Still holding p->lock from scheduler.
  release(&myproc()->lock);

// #if defined(MLFQ)
//   release(&queue[myproc()->queue].lock);
// #endif

  if (first) {
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    first = 0;
    fsinit(ROOTDEV);
  }

  usertrapret();
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.

  acquire(&p->lock);  //DOC: sleeplock1
  release(lk);

#if defined(PBS)
  if(p->state == RUNNING) {
    // ticks lock is already held
    p->tickrng = ticks - p->tickls;
    p->tickls = ticks;
    if(p->tickslp + p->tickrng == 0) p->niceness = 0;
    else p->niceness = (p->tickslp * 10) / (p->tickslp + p->tickrng);
  }
#endif

  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  release(&p->lock);
  acquire(lk);
}

// Wake up all processes sleeping on chan.
// Must be called without any p->lock.
void
wakeup(void *chan)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    if(p != myproc()){
      acquire(&p->lock);
      if(p->state == SLEEPING && p->chan == chan) {
#if defined(PBS)
        p->tickslp = ticks - p->tickls;
        p->tickls = ticks;
        if(p->tickslp + p->tickrng == 0) p->niceness = 0;
        else p->niceness = (p->tickslp * 10) / (p->tickslp + p->tickrng);
#endif
        p->state = RUNNABLE;
#if defined(MLFQ)
        p->ticksused = 0;
        p->intime = ticks;
#endif
#if defined(LBS)
        totaltickets += p->tickets;
#endif
      }
      release(&p->lock);
    }
  }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int
kill(int pid)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid){
      p->killed = 1;
      if(p->state == SLEEPING){
        // Wake process from sleep().
        p->state = RUNNABLE;
#if defined(PBS)
        p->tickslp = ticks - p->tickls;
        p->tickls = ticks;
        if(p->tickslp + p->tickrng == 0) p->niceness = 0;
        else p->niceness = (p->tickslp * 10) / (p->tickslp + p->tickrng);
#endif
#if defined(MLFQ)
        p->ticksused = 0;
        p->intime = ticks;
#endif
#if defined(LBS)
        totaltickets += p->tickets;
#endif
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

void
setkilled(struct proc *p)
{
  acquire(&p->lock);
  p->killed = 1;
  release(&p->lock);
}

int
killed(struct proc *p)
{
  int k;
  
  acquire(&p->lock);
  k = p->killed;
  release(&p->lock);
  return k;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if(user_dst){
    return copyout(p->pagetable, dst, src, len);
  } else {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if(user_src){
    return copyin(p->pagetable, dst, src, len);
  } else {
    memmove(dst, (char*)src, len);
    return 0;
  }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    = "unused",
  [USED]      = "used",
  [SLEEPING]  = "sleep ",
  [RUNNABLE]  = "runble",
  [RUNNING]   = "run   ",
  [ZOMBIE]    = "zombie"
  };
  struct proc *p;
  char *state;

  printf("\n");
  for(p = proc; p < &proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
#if defined (PBS)
    printf("%d %d %d %s %s", p->pid, p->priority, p->niceness, state, p->name);
#elif defined (FCFS)
    printf("%d %d %s %s", p->pid, p->stick, state, p->name);
#elif defined (MLFQ)
    printf("%d %d %d %d %d %s %s", p->pid, p->queue, p->intime, p->waittime, p->ticksused, state, p->name);
#elif defined (LBS)
    printf("%d %d %s %s", p->pid, p->tickets, state, p->name);
#else
    printf("%d %s %s", p->pid, state, p->name);
#endif
    printf("\n");
  }
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
waitx(uint64 addr, uint* wtime, uint* rtime)
{
  struct proc *np;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(np = proc; np < &proc[NPROC]; np++){
      if(np->parent == p){
        // make sure the child isn't still in exit() or swtch().
        acquire(&np->lock);

        havekids = 1;
        if(np->state == ZOMBIE){
          // Found one.
          pid = np->pid;
          *rtime = np->rtime;
          *wtime = np->etime - np->ctime - np->rtime;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&np->xstate,
                                  sizeof(np->xstate)) < 0) {
            release(&np->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(np);
          release(&np->lock);
          release(&wait_lock);
          return pid;
        }
        release(&np->lock);
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || p->killed){
      release(&wait_lock);
      return -1;
    }
    
    // Wait for a child to exit.
    sleep(p, &wait_lock);  //DOC: wait-sleep
  }
}
