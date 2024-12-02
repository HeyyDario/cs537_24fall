#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "pstat.h"
#include "spinlock.h"

extern struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

extern struct {
  int global_tickets;
  int global_stride;
  int global_pass;
} strideglobalinfo;

int
sys_fork(void)
{
  return fork();
}

int
sys_exit(void)
{
  exit();
  return 0;  // not reached
}

int
sys_wait(void)
{
  return wait();
}

int
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

int
sys_getpid(void)
{
  return myproc()->pid;
}

int
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

int
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

// return how many clock tick interrupts have occurred
// since start.
int
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

int sys_settickets(void) {
  int new_tickets;

  if (argint(0, &new_tickets) < 0) {
    return -1;
  }

  if (new_tickets < 1) {
    new_tickets = 8; // Default ticket count if less than 1
  } else if (new_tickets > (1 << 5)) {
    new_tickets = 1 << 5; // Maximum of 32
  }

  struct proc *p = myproc();
  int old_tickets = p->tickets;

  if (new_tickets == old_tickets) {
    return 0; 
  }

  acquire(&ptable.lock);

  // compute remain of the current stride
  int remain = p->pass - strideglobalinfo.global_pass;

  // adjust global ticket count
  strideglobalinfo.global_tickets -= p->tickets;
  p->tickets = new_tickets;
  strideglobalinfo.global_tickets += p->tickets;

  // recalculate the process's stride based on the new ticket count
  int old_stride = p->stride;
  p->stride = STRIDE1 / p->tickets;

  // scale the remain proportionally to the new stride
  remain = remain * p->stride / old_stride;

  // Update pass based on the adjusted remain
  p->pass = strideglobalinfo.global_pass + remain;

  // recalculate global stride
  if (strideglobalinfo.global_tickets > 0) {
    strideglobalinfo.global_stride = STRIDE1 / strideglobalinfo.global_tickets;
  } else {
    strideglobalinfo.global_stride = 0;
  }

  release(&ptable.lock);
  return 0; 
}


int sys_getpinfo(void) {
  struct pstat *ps;

  if (argptr(0, (void*)&ps, sizeof(*ps)) < 0) {
    return -1; 
  }

  acquire(&ptable.lock);

  struct proc *p;
  int i;
  for (i = 0, p = ptable.proc; p < &ptable.proc[NPROC]; i++, p++) {
    ps->inuse[i] = (p->state != UNUSED) ? 1 : 0;  //if the process slot is in use
    ps->tickets[i] = p->tickets;
    ps->pid[i] = p->pid;
    ps->pass[i] = p->pass;
    ps->remain[i] = p->remain;
    ps->stride[i] = p->stride;
    ps->rtime[i] = p->run_ticks; // total running time by ticks
  }

  release(&ptable.lock); 
  return 0;  
}



