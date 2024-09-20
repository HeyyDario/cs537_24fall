#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"

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

int
sys_getparentname(void)
{
  struct proc *curr = myproc();
  struct proc *parent = curr->parent;
  char* parentbuf;
  char* childbuf;
  int parentbufsize;
  int childbufsize;

  // Extract arguments from the system call
  if (argstr(0, &parentbuf) < 0 || argstr(1, &childbuf) < 0) {
    return -1; // Invalid arguments
  }
  if (argint(2, &parentbufsize) < 0 || argint(3, &childbufsize) < 0) {
    return -1; // Invalid size arguments
  }

  // Check for null pointers or invalid buffer sizes
  if (parentbuf == 0 || childbuf == 0 || parentbufsize <= 0 || childbufsize <= 0) {
    return -1;
  }

  // Copy parent name to parentbuf
  safestrcpy(parentbuf, parent->name, parentbufsize);

  // Copy current process name to childbuf
  safestrcpy(childbuf, curr->name, childbufsize);

  return 0;
}
