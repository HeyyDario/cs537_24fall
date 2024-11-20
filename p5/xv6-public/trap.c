#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"

struct file
{
  enum
  {
    FD_NONE,
    FD_PIPE,
    FD_INODE
  } type;
  int ref; // reference count
  char readable;
  char writable;
  struct pipe *pipe;
  struct inode *ip;
  uint off;
};

#define PAGE_SIZE 4096

// Interrupt descriptor table (shared by all CPUs).
struct gatedesc idt[256];
extern uint vectors[]; // in vectors.S: array of 256 entry pointers
struct spinlock tickslock;
uint ticks;

void tvinit(void)
{
  int i;

  for (i = 0; i < 256; i++)
    SETGATE(idt[i], 0, SEG_KCODE << 3, vectors[i], 0);
  SETGATE(idt[T_SYSCALL], 1, SEG_KCODE << 3, vectors[T_SYSCALL], DPL_USER);

  initlock(&tickslock, "time");
}

void idtinit(void)
{
  lidt(idt, sizeof(idt));
}

// PAGEBREAK: 41
void trap(struct trapframe *tf)
{
  if (tf->trapno == T_SYSCALL)
  {
    if (myproc()->killed)
      exit();
    myproc()->tf = tf;
    syscall();
    if (myproc()->killed)
      exit();
    return;
  }

  switch (tf->trapno)
  {
  case T_IRQ0 + IRQ_TIMER:
    if (cpuid() == 0)
    {
      acquire(&tickslock);
      ticks++;
      wakeup(&ticks);
      release(&tickslock);
    }
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE:
    ideintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE + 1:
    // Bochs generates spurious IDE1 interrupts.
    break;
  case T_IRQ0 + IRQ_KBD:
    kbdintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_COM1:
    uartintr();
    lapiceoi();
    break;
  case T_IRQ0 + 7:
  case T_IRQ0 + IRQ_SPURIOUS:
    cprintf("cpu%d: spurious interrupt at %x:%x\n",
            cpuid(), tf->cs, tf->eip);
    lapiceoi();
    break;
  case T_PGFLT: {
    uint fault_addr = rcr2(); // Address causing the fault
    struct proc *p = myproc();

    // Find the mapping containing fault_addr
    for (int i = 0; i < p->wmap_data.total_mmaps; i++) {
        uint start = p->wmap_data.addr[i];
        uint end = start + p->wmap_data.length[i];

        // Check if the faulting address falls within this mapping
        if (fault_addr >= start && fault_addr < end) {
            uint page_addr = PGROUNDDOWN(fault_addr);
            char *mem = kalloc();
            if (!mem) {
                panic("trap: lazy allocation failed: out of memory");
            }
            memset(mem, 0, PGSIZE);

            // Map the page into the process's address space
            if (map_pages(p->pgdir, (void *)page_addr, PGSIZE, V2P(mem), PTE_W | PTE_U) < 0) {
                kfree(mem);
                panic("trap: failed to map page");
            }

            // Handle file-backed mappings
            if (!(p->wmap_data.flags[i] & MAP_ANONYMOUS)) {
                struct file *f = p->ofile[p->wmap_data.fd[i]];
                cprintf("trap: fd %d -> file pointer %p\n", p->wmap_data.fd[i], f);
                if (!f) {
                    cprintf("trap: fd %d has NULL file\n", p->wmap_data.fd[i]);
                    panic("trap: file not found for file-backed mapping");
                }
                int offset = fault_addr - start;
                f->off = offset;
                int bytes_read = fileread(f, mem, PGSIZE);
                if (bytes_read < 0) {
                    //kfree(mem);
                    panic("trap: failed to read file");
                }
                
                // Zero out the remainder of the page if the file does not fully occupy it
                if (bytes_read < PGSIZE) {
                    memset(mem + bytes_read, 0, PGSIZE - bytes_read);
                }
            }
            p->wmap_data.n_loaded_pages[i]++;
            lcr3(V2P(p->pgdir)); // Flush the TLB
            return;
        }
    }

    // Address not found in mappings - segmentation fault
    cprintf("Segmentation Fault at address 0x%x\n", fault_addr);
    p->killed = 1;
    return;
}
  // PAGEBREAK: 13
  default:
    if (myproc() == 0 || (tf->cs & 3) == 0)
    {
      // In kernel, it must be our mistake.
      cprintf("unexpected trap %d from cpu %d eip %x (cr2=0x%x)\n",
              tf->trapno, cpuid(), tf->eip, rcr2());
      panic("trap");
    }
    // In user space, assume process misbehaved.
    cprintf("pid %d %s: trap %d err %d on cpu %d "
            "eip 0x%x addr 0x%x--kill proc\n",
            myproc()->pid, myproc()->name, tf->trapno,
            tf->err, cpuid(), tf->eip, rcr2());
    myproc()->killed = 1;
  }

  // Force process exit if it has been killed and is in user space.
  // (If it is still executing in the kernel, let it keep running
  // until it gets to the regular system call return.)
  if (myproc() && myproc()->killed && (tf->cs & 3) == DPL_USER)
    exit();

  // Force process to give up CPU on clock tick.
  // If interrupts were on while locks held, would need to check nlock.
  if (myproc() && myproc()->state == RUNNING &&
      tf->trapno == T_IRQ0 + IRQ_TIMER)
    yield();

  // Check if the process has been killed since we yielded
  if (myproc() && myproc()->killed && (tf->cs & 3) == DPL_USER)
    exit();
}
