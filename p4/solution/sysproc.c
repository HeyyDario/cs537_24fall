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

// System call to set the ticket count for the current process
int sys_settickets(void) {
  int n;

  // Retrieve the argument passed to the system call
  if (argint(0, &n) < 0) {
    return -1; // Error if no valid argument
  }

  // Clamp n to be within the allowed range
  if (n < 1) {
    n = 8; // Default ticket count if n < 1
  } else if (n > (1 << 5)) {
    n = 1 << 5; // Max ticket count is 32
  }

  struct proc *p = myproc();

  acquire(&ptable.lock);

  // Update global tickets by removing old ticket count and adding the new count
  strideglobalinfo.global_tickets -= p->tickets;
  p->tickets = n;
  strideglobalinfo.global_tickets += p->tickets;

  // Recalculate the process's stride based on its new ticket count
  p->stride = STRIDE1 / p->tickets;

  // Update global stride after changing total tickets
  if (strideglobalinfo.global_tickets > 0) {
    strideglobalinfo.global_stride = STRIDE1 / strideglobalinfo.global_tickets;
  } else {
    strideglobalinfo.global_stride = 0;
  }

  release(&ptable.lock);

  return 0; // Success
}

int sys_getpinfo(void) {
  struct pstat *ps;

  // Retrieve the pointer to the pstat structure passed from user space
  if (argptr(0, (void*)&ps, sizeof(*ps)) < 0) {
    return -1; // Return an error if the argument pointer is invalid
  }

  acquire(&ptable.lock);  // Acquire the process table lock

  struct proc *p;
  int i;
  for (i = 0, p = ptable.proc; p < &ptable.proc[NPROC]; i++, p++) {
    ps->inuse[i] = (p->state != UNUSED) ? 1 : 0;  // Check if the process slot is in use
    ps->tickets[i] = p->tickets;
    ps->pid[i] = p->pid;
    ps->pass[i] = p->pass;
    ps->remain[i] = p->remain;
    ps->stride[i] = p->stride;
    ps->rtime[i] = p->run_ticks; // Total running time (ticks)
  }

  release(&ptable.lock);  // Release the process table lock
  return 0;  // Success
}



