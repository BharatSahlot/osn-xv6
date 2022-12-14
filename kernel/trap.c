#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct spinlock tickslock;
uint ticks;

extern char trampoline[], uservec[], userret[];

// in kernelvec.S, calls kerneltrap().
void kernelvec();

extern int devintr();

void
trapinit(void)
{
  initlock(&tickslock, "time");
}

// set up to take exceptions and traps while in the kernel.
void
trapinithart(void)
{
  w_stvec((uint64)kernelvec);
}

//
// handle an interrupt, exception, or system call from user space.
// called from trampoline.S
//
void
usertrap(void)
{
  int which_dev = 0;

  if((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");

  // send interrupts and exceptions to kerneltrap(),
  // since we're now in the kernel.
  w_stvec((uint64)kernelvec);

  struct proc *p = myproc();
  
  // save user program counter.
  p->trapframe->epc = r_sepc();
  
  if(r_scause() == 8){
    // system call

    if(killed(p))
      exit(-1);

    // sepc points to the ecall instruction,
    // but we want to return to the next instruction.
    p->trapframe->epc += 4;

    // an interrupt will change sepc, scause, and sstatus,
    // so enable only now that we're done with those registers.
    intr_on();

    syscall();
  } else if((which_dev = devintr()) != 0){
    // ok
  } else {
    if(r_scause() == 15 || r_scause() == 13){
      uint64 va = r_stval();
      if(va >= MAXVA || (va >= PGROUNDDOWN(p->trapframe->sp) - PGSIZE && va <= PGROUNDDOWN(p->trapframe->sp)))
      {
        setkilled(p);
        exit(-1);
      }
      pte_t *pte;
      uint64 pa;
      int flags;
      va = PGROUNDDOWN(va);
      pte = walk(p->pagetable, va, 0);
      if(pte == 0)
      {
        setkilled(p);
        exit(-1);
      }
      pa = PTE2PA(*pte);
      flags = PTE_FLAGS(*pte);
      if(!(flags & PTE_V))
      {
        printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
        printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
        setkilled(p);
        exit(-1);
      }
      if(!(flags & PTE_U))
      {
        printf("User cannot access memory\n");
        setkilled(p);
        exit(-1);
      }
      if(flags & PTE_C)
      {
        flags &= ~PTE_C;
        flags |= PTE_W;
        char *mem = kalloc();
        if(mem == 0)
        {
          printf("Couldn't allocate memory\n");
          setkilled(p);
          exit(-1);
        }
        memmove(mem, (void *)pa, PGSIZE);
        *pte = PA2PTE(mem) | flags;
        kfree((void *)pa);
      }
      else
      {
        printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
        printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
        setkilled(p);
      }
    }
    else
    {
      printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
      printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
      setkilled(p);
    }
  }

  if(killed(p))
    exit(-1);

#if defined(FCFS) || defined(PBS)
  if(which_dev == 2){
    p->ticksp++;
    if(p->ticksn > 0 && p->ticksp - p->tickspa == p->ticksn && p->sigalarm)
    {
      p->sigalarm = 0;
      *(p->trapcopy) = *(p->trapframe);
      p->trapframe->epc = p->handler;
    }
  }
#else
  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2){
    p->ticksp++;
    if(p->ticksn > 0 && p->ticksp - p->tickspa == p->ticksn && p->sigalarm)
    {
      p->sigalarm = 0;
      *(p->trapcopy) = *(p->trapframe);
      p->trapframe->epc = p->handler;
    }
#if defined(MLFQ)
    p->ticksused++;
    if(p->ticksused >= (1 << p->queue)) yield();
    else {
      struct proc* tp;
      for(tp = proc; tp < &proc[NPROC]; tp++) {
        acquire(&tp->lock);
        if(tp->state == RUNNABLE && tp->queue < p->queue) {
          release(&tp->lock);
          yield();
          break;
        }
        release(&tp->lock);
      }
    }
#else
    yield();
#endif
  }
#endif

  usertrapret();
}

//
// return to user space
//
void
usertrapret(void)
{
  struct proc *p = myproc();

  // we're about to switch the destination of traps from
  // kerneltrap() to usertrap(), so turn off interrupts until
  // we're back in user space, where usertrap() is correct.
  intr_off();

  // send syscalls, interrupts, and exceptions to uservec in trampoline.S
  uint64 trampoline_uservec = TRAMPOLINE + (uservec - trampoline);
  w_stvec(trampoline_uservec);

  // set up trapframe values that uservec will need when
  // the process next traps into the kernel.
  p->trapframe->kernel_satp = r_satp();         // kernel page table
  p->trapframe->kernel_sp = p->kstack + PGSIZE; // process's kernel stack
  p->trapframe->kernel_trap = (uint64)usertrap;
  p->trapframe->kernel_hartid = r_tp();         // hartid for cpuid()

  // set up the registers that trampoline.S's sret will use
  // to get to user space.
  
  // set S Previous Privilege mode to User.
  unsigned long x = r_sstatus();
  x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode
  x |= SSTATUS_SPIE; // enable interrupts in user mode
  w_sstatus(x);

  // set S Exception Program Counter to the saved user pc.
  w_sepc(p->trapframe->epc);

  // tell trampoline.S the user page table to switch to.
  uint64 satp = MAKE_SATP(p->pagetable);

  // jump to userret in trampoline.S at the top of memory, which 
  // switches to the user page table, restores user registers,
  // and switches to user mode with sret.
  uint64 trampoline_userret = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64))trampoline_userret)(satp);
}

// interrupts and exceptions from kernel code go here via kernelvec,
// on whatever the current kernel stack is.
void 
kerneltrap()
{
  int which_dev = 0;
#if !defined(FCFS) && !defined(PBS)
  uint64 sepc = r_sepc();
#endif
  uint64 sstatus = r_sstatus();
  uint64 scause = r_scause();
  
  if((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not from supervisor mode");
  if(intr_get() != 0)
    panic("kerneltrap: interrupts enabled");

  if((which_dev = devintr()) == 0){
    printf("scause %p\n", scause);
    printf("sepc=%p stval=%p\n", r_sepc(), r_stval());
    panic("kerneltrap");
  }

#if defined(FCFS) || defined(PBS)
  // dont give up CPU in non-premptive scheduling
#else
  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2 && myproc() != 0 && myproc()->state == RUNNING) {
#if defined(MLFQ)
    struct proc* p = myproc();
    p->ticksused++;
    if(p->ticksused >= (1 << p->queue)) yield();
    else {
      struct proc* tp;
      for(tp = proc; tp < &proc[NPROC]; tp++) {
        acquire(&tp->lock);
        if(tp->state == RUNNABLE && tp->queue < p->queue) {
          release(&tp->lock);
          yield();
          break;
        }
        release(&tp->lock);
      }
    }
#else
    yield();
#endif
  }

  // the yield() may have caused some traps to occur,
  // so restore trap registers for use by kernelvec.S's sepc instruction.
  w_sepc(sepc);
  w_sstatus(sstatus);
#endif
}

void
clockintr()
{
  acquire(&tickslock);
  ticks++;
  struct proc* p;
  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state == RUNNABLE) {
#if defined(MLFQ)
      p->waittime++;
      if(p->waittime >= MAX_WAIT_TIME) {
        p->waittime = 0;
#if defined(TRACE_QUEUE)
        printf("[%d] queue for %d changed from %d to %d\n", ticks, p->pid, p->queue, p->queue - 1 < 0 ? 0 : p->queue - 1);
#endif
        p->queue--;
        if(p->queue < 0) p->queue = 0;
      }
#endif
    } else if (p->state == RUNNING) {
      p->rtime++;
    }
    release(&p->lock);
  }
  wakeup(&ticks);
  release(&tickslock);
}

// check if it's an external interrupt or software interrupt,
// and handle it.
// returns 2 if timer interrupt,
// 1 if other device,
// 0 if not recognized.
int
devintr()
{
  uint64 scause = r_scause();

  if((scause & 0x8000000000000000L) &&
     (scause & 0xff) == 9){
    // this is a supervisor external interrupt, via PLIC.

    // irq indicates which device interrupted.
    int irq = plic_claim();

    if(irq == UART0_IRQ){
      uartintr();
    } else if(irq == VIRTIO0_IRQ){
      virtio_disk_intr();
    } else if(irq){
      printf("unexpected interrupt irq=%d\n", irq);
    }

    // the PLIC allows each device to raise at most one
    // interrupt at a time; tell the PLIC the device is
    // now allowed to interrupt again.
    if(irq)
      plic_complete(irq);

    return 1;
  } else if(scause == 0x8000000000000001L){
    // software interrupt from a machine-mode timer interrupt,
    // forwarded by timervec in kernelvec.S.

    if(cpuid() == 0){
      clockintr();
    }
    
    // acknowledge the software interrupt by clearing
    // the SSIP bit in sip.
    w_sip(r_sip() & ~2);

    return 2;
  } else {
    return 0;
  }
}

