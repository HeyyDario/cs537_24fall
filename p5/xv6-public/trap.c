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
  case T_PGFLT:
  {
    uint fault_addr = rcr2(); // Address causing the fault
    uint vaddr = PGROUNDDOWN(fault_addr);
    struct proc *p = myproc();
    pte_t *pte = get_pte(p->pgdir, (void *)fault_addr, 0);
    //cprintf("PID=%d va=%x pa=%x pte=%x.\n", p->pid, vaddr, V2P(vaddr), pte);

    // Step 1: Check if fault address is part of memory-mapped regions (lazy allocation)
    if (!pte || !(*pte & PTE_P))
    {
      // iterates through all mappings to find if fault_addr lies within one of these regions
      for (int i = 0; i < p->wmap_data.total_mmaps; i++)
      {
        uint start = p->wmap_data.addr[i];
        uint end = start + p->wmap_data.length[i];

        // Check if the faulting address falls within this mapping
        if (fault_addr >= start && fault_addr < end)
        {
          // Perform lazy allocation
          char *mem = kalloc();
          if (!mem)
          {
            panic("trap: lazy allocation failed: out of memory\n");
          }
          memset(mem, 0, PGSIZE); // allocates a physical page only when the process accesses it for the first time.

          // Map the page into the process's address space
          if (map_pages(p->pgdir, (void *)vaddr, PGSIZE, V2P(mem), PTE_W | PTE_U) < 0)
          {
            kfree(mem);
            panic("trap: failed to map page\n");
          }

          // Handle file-backed mappings
          if (!(p->wmap_data.flags[i] & MAP_ANONYMOUS))
          {
            cprintf("file-backed mapping start!\n");
            struct file *f = p->ofile[p->wmap_data.fd[i]];
            if (!f)
            {
              panic("trap: file-backed mapping failed\n");
            }
            int offset = vaddr - start;
            int n_bytes = PGSIZE;
            if (vaddr + PGSIZE > end)
            {
              n_bytes = end - vaddr;
            }

            // int r;
            ilock(f->ip);
            // r = readi(f->ip, (char *)vaddr, offset, n_bytes);
            int bytes_read = readi(f->ip, (char *)P2V(V2P(mem)), offset, n_bytes);
            iunlock(f->ip);
            if (bytes_read < 0)
            {
              cprintf("readi failed\n");
              panic("trap: file-backed mapping read failed\n");
              // return -1;
            }

            // Zero remaining space in the page
            if (bytes_read < PGSIZE)
            {
              memset(mem + bytes_read, 0, PGSIZE - bytes_read);
            }
          }

          // cprintf("end file-backed mapping.\n");
          p->wmap_data.n_loaded_pages[i]++;
          lcr3(V2P(p->pgdir)); // Flush the TLB
          return;
        } // end if (fault_addr >= start && fault_addr < end)
      } // end for
    } // end if (!pte || !(*pte & PTE_P))

      // Handle Copy-On-Write
      if (*pte & PTE_COW)
      {
        uint pa = PTE_ADDR(*pte); // Extract physical address
        int ref_c = getref(pa);
        if (ref_c > 1) // Shared page: Allocate a new page
        {
          char *new_page = kalloc();
          if (!new_page)
          {
            panic("Page fault: out of memory during COW handling\n");
          }

          memmove(new_page, (char *)P2V(PTE_ADDR(*pte)), PGSIZE);
          // Map the new page and restore write permissions
          // map_pages(p->pgdir, (void *)fault_addr, PGSIZE, V2P(new_page), PTE_W | PTE_U);
          uint new_pa = V2P(new_page); // Get the physical address of the newly allocated page
          int flags = PTE_FLAGS(*pte);
          flags &= ~PTE_COW; // Clear the PTE_COW flag
          flags |= PTE_W;    // make the page writable
          // cprintf("COW: fault_addr: x%x, new_pa: x%x, flags: %d with rc %d\n", fault_addr, new_pa, flags, ref_c);
          *pte = new_pa | flags; // Update the PTE to point to the new physical page with updated flags
          lcr3(V2P(myproc()->pgdir));
          // cprintf("before inc, new_pa ref count: %d\n", getref(new_pa));
          incref(new_pa);
          // cprintf("after inc, new_pa ref count: %d\n", getref(new_pa));
          // cprintf("before dec, pa ref count: %d\n", getref(pa));
          decref(pa);
          // cprintf("after dec, pa ref count: %d\n", getref(pa));
          cprintf("COW: Allocated new page at 0x%x for fault_addr 0x%x\n", new_pa, fault_addr);
        }
        else if (ref_c == 1) // unshared page
        {
          // cprintf("COW: fault_addr: x%x, modified existig as rc %d\n", fault_addr, ref_c);
          int flags = PTE_FLAGS(*pte);
          flags &= ~PTE_COW; // Clear the PTE_COW flag
          flags |= PTE_W;    // make the page writable
          *pte = pa | flags; // Update the PTE to make the original page writable with updated flags
          lcr3(V2P(myproc()->pgdir));
          cprintf("COW: Made existing page writable at 0x%x\n", pa);
        }
        else
        {
          uint pa = PTE_ADDR(*pte);
          cprintf("COW: pa ref count: %d\n", getref(pa));
          panic("COW: RC < 1");
        }
        lcr3(V2P(p->pgdir));  // Flush the TLB
        return; // break before
      } // endif
      // else
      // {
      //   // Originally read-only page: Kill the process
      //   cprintf("Segmentation Fault2 at address 0x%x\n", fault_addr);
      //   p->killed = 1;
      // }

      // Address not found in mappings - segmentation fault
      // cprintf("Segmentation Fault at3 address 0x%x\n", fault_addr);
      // p->killed = 1;
      // return;
    
    // Address not found in mappings - segmentation fault
    cprintf("Segmentation Fault at address 0x%x\n", fault_addr);
    p->killed = 1;
    return; // or break?
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
