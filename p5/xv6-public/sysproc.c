#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "wmap.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"

#define PAGE_SIZE 4096
uint wmap(uint addr, int length, int flags, int fd);

int sys_fork(void)
{
  return fork();
}

int sys_exit(void)
{
  exit();
  return 0; // not reached
}

int sys_wait(void)
{
  return wait();
}

int sys_kill(void)
{
  int pid;

  if (argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

int sys_getpid(void)
{
  return myproc()->pid;
}

int sys_sbrk(void)
{
  int addr;
  int n;

  if (argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if (growproc(n) < 0)
    return -1;
  return addr;
}

int sys_sleep(void)
{
  int n;
  uint ticks0;

  if (argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while (ticks - ticks0 < n)
  {
    if (myproc()->killed)
    {
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
int sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

int sys_wmap(void)
{
  uint addr;
  int length, flags, fd;

  // Fetch arguments from the user stack
  if (argint(0, (int *)&addr) < 0 ||
      argint(1, &length) < 0 ||
      argint(2, &flags) < 0 ||
      argint(3, &fd) < 0)
  {
    return FAILED;
  }

  // Call the core wmap function
  return (int)wmap(addr, length, flags, fd);
}

uint wmap(uint addr, int length, int flags, int fd) {
    struct proc *p = myproc();

    // Validate flags: Must include MAP_FIXED and MAP_SHARED
    if (!(flags & MAP_FIXED) || !(flags & MAP_SHARED)) {
        return FAILED;
    }

    // Validate address: Must be in the range [0x60000000, 0x80000000) and page-aligned
    if (addr < 0x60000000 || addr >= 0x80000000 || addr % PAGE_SIZE != 0) {
        return FAILED;
    }

    // Validate length: Must be greater than 0 and a multiple of page size
    if (length <= 0 || length % PAGE_SIZE != 0) {
        return FAILED;
    }

    // Validate file descriptor for file-backed mapping (if not anonymous)
    if (!(flags & MAP_ANONYMOUS)) {
        struct file *f = p->ofile[fd];
        if (!f || f->type != FD_INODE || !(f->readable && f->writable)) {
            return FAILED;
        }
    }

    // Check if there is space for another mapping
    if (p->wmap_data.total_mmaps >= MAX_WMMAP_INFO) {
        return FAILED;
    }

    // Record the mapping in wmap_data
    int index = p->wmap_data.total_mmaps;
    p->wmap_data.addr[index] = addr;
    p->wmap_data.length[index] = length;
    p->wmap_data.n_loaded_pages[index] = 0;
    p->wmap_data.total_mmaps++;

    // Return the starting address of the mapping
    return addr;
}