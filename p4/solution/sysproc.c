#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "pstat.h"

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

int sys_settickets_pid(void) {
    int pid, new_tickets;

    if (argint(0, &pid) < 0 || argint(1, &new_tickets) < 0 || new_tickets < 1)
        return -1;

    struct proc *p;
    struct spinlock *lock = getptablelock();
    acquire(lock);
    
    for (p = getptable(); p < getptable() + NPROC; p++) {
        if (p->pid == pid) {
            // Remove the current process's tickets from global tickets
            int global_tickets = get_global_tickets();
            global_tickets -= p->tickets;
            set_global_tickets(global_tickets);

            // Recalculate global_stride based on the new total tickets
            if (global_tickets > 0) {
                set_global_stride(STRIDE1 / global_tickets);
            } else {
                set_global_stride(0);
            }

            // Update the process's tickets and recalculate stride and pass
            int old_stride = p->stride;        // Store the old stride value
            p->tickets = new_tickets;          // Update tickets
            p->stride = STRIDE1 / new_tickets; // Recalculate the new stride

            // Scale remain by new_stride / old_stride to adjust pass correctly
            if (old_stride > 0) {
                p->remain = (p->remain * p->stride) / old_stride;
            }

            // Update the process's pass using the adjusted remain value
            p->pass = get_global_pass() + p->remain;

            // Add the updated tickets back to global tickets and recalculate global stride
            global_tickets += new_tickets;
            set_global_tickets(global_tickets);
            set_global_stride(STRIDE1 / global_tickets);

            release(lock);
            return 0;
        }
    }
    release(lock);
    return -1; // Process not found
}

int sys_settickets(void) {
    int n;
    struct proc *p = myproc(); // Get the current process

    if (argint(0, &n) < 0)
        return -1;

    // Clamp n to the allowed ticket range
    if (n < 1)
        n = 8;               // Default to 8 if n is less than 1
    else if (n > (1 << 5))   // Maximum of 32 tickets
        n = 1 << 5;

    acquire_ptable_lock(); // Lock ptable

    // Update global tickets and process tickets
    int global_tickets = get_global_tickets();
    global_tickets -= p->tickets;   // Remove current tickets from global count
    p->tickets = n;                 // Set new ticket value
    global_tickets += p->tickets;   // Add new tickets to global count
    set_global_tickets(global_tickets);

    // Recalculate stride and pass based on new ticket count
    p->stride = STRIDE1 / p->tickets;
    if (global_tickets > 0)
        set_global_stride(STRIDE1 / global_tickets);

    // Adjust remain and pass values
    int old_stride = p->stride;
    if (old_stride > 0) {
        p->remain = (p->remain * p->stride) / old_stride;
    }
    p->pass = get_global_pass() + p->remain;

    release_ptable_lock(); // Unlock ptable
    return 0;
}

int sys_getpinfo(void) {
    struct pstat *ps;

    // Retrieve the pointer to the pstat structure passed from user space
    if (argptr(0, (void*)&ps, sizeof(*ps)) < 0)
        return -1;

    acquire_ptable_lock(); // Lock the process table

    struct proc *p;
    int i;
    for (i = 0, p = getptable(); p < getptable() + NPROC; i++, p++) {
        ps->inuse[i] = (p->state != UNUSED) ? 1 : 0;
        ps->tickets[i] = p->tickets;
        ps->pid[i] = p->pid;
        ps->pass[i] = p->pass;
        ps->remain[i] = p->remain;
        ps->stride[i] = p->stride;
        ps->rtime[i] = p->run_ticks; // Total running time (ticks)
    }

    release_ptable_lock(); // Unlock the process table
    return 0; // Success
}



