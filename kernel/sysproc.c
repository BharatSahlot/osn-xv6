#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "syscall.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  uint64 r = fork();
  return r;
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);

  uint64 r = wait(p);
  return r;
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int n;

  argint(0, &n);
  addr = myproc()->sz;
  if(growproc(n) < 0)
  {
    return -1;
  }
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  uint64 r = kill(pid);
  return r;
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

uint64
sys_sigalarm(void)
{
  struct proc *p = myproc();
  int n;
  uint64 addr;
  argint(0, &n);
  argaddr(1, &addr);
  acquire(&p->lock);
  if(n > 0)
  {
    p->sigalarm = 1;
  }
  else
  {
    p->sigalarm = 0;
  }
  p->ticksn = n;
  p->tickspa = p->ticksp;
  p->handler = addr;
  release(&p->lock);
  return 0;
}


uint64
sys_sigreturn(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  p->trapcopy->kernel_hartid = p->trapframe->kernel_hartid;
  p->trapcopy->kernel_satp = p->trapframe->kernel_satp;
  p->trapcopy->kernel_sp = p->trapframe->kernel_sp;
  p->trapcopy->kernel_trap = p->trapframe->kernel_trap;
  *(p->trapframe) = *(p->trapcopy);
  if(p->ticksn > 0)
  {
    p->sigalarm = 1;
  }
  else
  {
    p->sigalarm = 0;
  }
  p->tickspa = p->ticksp;
  release(&p->lock);
  return p->trapframe->a0;
}

uint64
sys_trace(void)
{
  int mask;
  struct proc* p;

  argint(0, &mask);

  p = myproc();
  p->trace = 1;
  p->tracemask = mask;
  return 0;
}

#if defined(PBS)
uint64
sys_set_priority(void)
{
  int np, pid;

  argint(0, &np);
  argint(1, &pid);

  return set_priority(np, pid);
}
#endif


#if defined(LBS)
uint64
sys_settickets(void)
{
  int n;
  struct proc *p = myproc();
  argint(0, &n);
  if(n < 0)
  {
    return -1;
  }
  acquire(&p->lock);
  p->tickets = n;
  release(&p->lock);
  return 0;
}
#endif
