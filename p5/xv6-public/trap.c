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
    struct proc *p = myproc();
    pte_t *pte;

    // Get the page table entry for the faulting address
    if ((pte = get_pte(p->pgdir, (void *)fault_addr, 0)) == 0 || !(*pte & PTE_P))
    {
      // Invalid page access: address is not mapped or present
      cprintf("Segmentation Fault at address 0x%x\n", fault_addr);
      p->killed = 1;
      break;
    }
    // cprintf("pte : %x\n",*pte);
    // Handle Copy-On-Write
    if (*pte & PTE_COW)
    {
      // Allocate a new physical page
      uint pa = PTE_ADDR(*pte);
      int ref_c = getref(pa);
      if (ref_c > 1)
      {
        char *new_page = kalloc();
        if (!new_page)
        {
          panic("Page fault: out of memory during COW handling");
        }

        // Copy the contents of the old page to the new page
        memmove(new_page, (char *)P2V(PTE_ADDR(*pte)), PGSIZE);
        // Map the new page and restore write permissions
        // map_pages(p->pgdir, (void *)fault_addr, PGSIZE, V2P(new_page), PTE_W | PTE_U);
        uint new_pa = V2P(new_page);
        int flags = PTE_FLAGS(*pte);
        flags &= ~PTE_COW;
        flags |= PTE_W;
        //cprintf("COW: fault_addr: x%x, new_pa: x%x, flags: %d with rc %d\n", fault_addr, new_pa, flags, ref_c);
        *pte = new_pa | flags;
        lcr3(V2P(myproc()->pgdir));
        incref(new_pa);
        decref(pa);
      }
      else if (ref_c == 1)
      {
        //cprintf("COW: fault_addr: x%x, modified existig as rc %d\n", fault_addr, ref_c);
        int flags = PTE_FLAGS(*pte);
        flags &= ~PTE_COW;
        flags |= PTE_W;
        *pte = pa | flags;
        lcr3(V2P(myproc()->pgdir));
      }
      else
      {
        panic("COW: RC < 1");
      }
      break;
    }
    else
    {
      // Originally read-only page: Kill the process
      cprintf("Segmentation Fault at address 0x%x\n", fault_addr);
      p->killed = 1;
    }

    if (*pte & PTE_W)
    {
      // Unexpected page fault on a writable page
      panic("trap: unexpected page fault on writable page");
    }
    // else if ((*pte & PTE_P) && !(*pte & PTE_W))
    // {
    //   // Handle Copy-on-Write
    //   uint pa = PTE_ADDR(*pte); // Physical address
    //   char *mem;

    //   if (getref(pa) > 1)
    //   {
    //     // Shared page: Allocate a new page and copy the contents
    //     if ((mem = kalloc()) == 0)
    //     {
    //       cprintf("trap: out of memory\n");
    //       p->killed = 1;
    //       return;
    //     }
    //     memmove(mem, (char *)P2V(pa), PGSIZE);   // Copy contents to new page
    //     decref(pa);                              // Decrement reference count for the original page
    //     *pte = V2P(mem) | PTE_W | PTE_U | PTE_P; // Update PTE to point to the new page
    //   }
    //   else
    //   {
    //     // Unshared page: Make the page writable
    //     *pte |= PTE_W;
    //   }

    //   lcr3(V2P(p->pgdir)); // Flush the TLB
    //   return;
    // }

    // iterates through all mappings to find if fault_addr lies within one of these regions
    for (int i = 0; i < p->wmap_data.total_mmaps; i++)
    {
      uint start = p->wmap_data.addr[i];
      uint end = start + p->wmap_data.length[i];

      // Check if the faulting address falls within this mapping
      if (fault_addr >= start && fault_addr < end)
      {
        uint page_addr = PGROUNDDOWN(fault_addr); // rounds down the faulting address to the nearest page boundary
        char *mem = kalloc();
        if (!mem)
        {
          panic("trap: lazy allocation failed: out of memory");
        }
        memset(mem, 0, PGSIZE); // allocates a physical page only when the process accesses it for the first time.

        // Map the page into the process's address space
        if (map_pages(p->pgdir, (void *)page_addr, PGSIZE, V2P(mem), PTE_W | PTE_U) < 0)
        {
          kfree(mem);
          panic("trap: failed to map page");
        }

        // Handle file-backed mappings
        if (!(p->wmap_data.flags[i] & MAP_ANONYMOUS))
        {
          // retrieves the file object
          struct file *f = p->ofile[p->wmap_data.fd[i]];
          if (!f)
          {
            panic("trap: file not found for file-backed mapping");
          }

          // computes the offset within the file
          int offset = fault_addr - start;
          f->off = offset;
          // read data from the file into memory
          int bytes_read = fileread(f, mem, PGSIZE);
          if (bytes_read < 0)
          {
            // kfree(mem);
            panic("trap: failed to read file");
          }
          // fills the rest of the page with zero if the file doesn’t occupy the entire page
          if (bytes_read < PGSIZE)
          {
            memset(mem + bytes_read, 0, PGSIZE - bytes_read);
          }
          //*pte &= ~PTE_W;  // Ensure file-backed pages remain read-only unless explicitly writable
        }

        // **Handle Anonymous Mappings**
        // Nothing additional to do, as the memory is already zero-initialized

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
