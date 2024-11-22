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
int wunmap(uint addr);
uint va2pa(uint va);

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

uint wmap(uint addr, int length, int flags, int fd)
{
    struct proc *p = myproc();

    // Validate flags: Must include MAP_FIXED and MAP_SHARED
    if (!(flags & MAP_FIXED) || !(flags & MAP_SHARED))
    {
        cprintf("wmap: Invalid flags 0x%x\n", flags);
        return FAILED;
    }

    // Validate address: Must be in the range [0x60000000, 0x80000000) and page-aligned
    if (addr < 0x60000000 || addr >= 0x80000000 || addr % PAGE_SIZE != 0)
    {
        cprintf("wmap: Invalid address 0x%x\n", addr);
        return FAILED;
    }

    // Validate length: Must be greater than 0
    if (length <= 0)
    {
        cprintf("wmap: Invalid length %d\n", length);
        return FAILED;
    }

    // Validate that mapping does not exceed allowed address space
    if (addr + length > 0x80000000)
    {
        cprintf("wmap: Mapping exceeds allowed address space\n");
        return FAILED;
    }

    uint new_start = addr;
    uint new_end = addr + length;

    for (int i = 0; i < p->wmap_data.total_mmaps; i++)
    {
        uint existing_start = p->wmap_data.addr[i];
        uint existing_end = existing_start + p->wmap_data.length[i];

        // Check for overlap
        if (!(new_end <= existing_start || new_start >= existing_end))
        {
            cprintf("wmap: Mapping overlaps with existing mapping at index %d\n", i);
            return FAILED;
        }
    }

    // Check if there is space for another mapping
    if (p->wmap_data.total_mmaps >= MAX_WMMAP_INFO)
    {
        cprintf("wmap: No space for more mappings\n");
        return FAILED;
    }

    // Record the mapping in wmap_data
    int index = p->wmap_data.total_mmaps;

    if (!(flags & MAP_ANONYMOUS))
    {
        struct file *f = p->ofile[fd];
        if (!f || f->type != FD_INODE || !(f->readable))
        {
            cprintf("wmap: Invalid file descriptor %d\n", fd);
            return FAILED;
        }
        // Duplicate the file
        struct file *dup_file = filedup(f);
        if (!dup_file)
        {
            // Handle error if duplication fails
            return -1;
        }

        // Allocate a new file descriptor for the duplicated file
        int new_fd = -1;
        for (int i = 0; i < NOFILE; i++)
        {
            if (myproc()->ofile[i] == 0)
            {
                myproc()->ofile[i] = dup_file;
                new_fd = i;
                break;
            }
        }

        if (new_fd == -1)
        {
            fileclose(dup_file); // Close the file if no descriptor is available
            return -1;
        }
        p->wmap_data.fd[index] = new_fd;
    }
    else
    {
        p->wmap_data.fd[index] = -1;
    }

    p->wmap_data.addr[index] = addr;
    p->wmap_data.length[index] = length;
    p->wmap_data.n_loaded_pages[index] = 0;
    p->wmap_data.flags[index] = flags;
    // p->wmap_data.fd[index] = (flags & MAP_ANONYMOUS) ? -1 : fd;
    p->wmap_data.total_mmaps++;

    cprintf("wmap: Added mapping at index %d, addr 0x%x, length %d\n", index, addr, length);

    // Return the starting address of the mapping
    return addr;
}

int sys_wunmap(void)
{
    uint addr;

    if (argint(0, (int *)&addr) < 0)
    {
        return FAILED;
    }

    return wunmap(addr);
}

// int wunmap(uint addr)
// {
//     struct proc *p = myproc();
//     int index = -1;

//     // Validate the address (must be page-aligned)
//     if (addr % PGSIZE != 0) {
//         return FAILED;
//     }

//     // Find the mapping in wmap_data
//     for (int i = 0; i < p->wmap_data.total_mmaps; i++) {
//         if (p->wmap_data.addr[i] == addr) {
//             index = i;
//             uint endAddr = addr + p->wmap_data.length[i];
//             int isShared = p->wmap_data.flags[i] & MAP_SHARED; // Check if it's a shared mapping

//             struct file *f = p->ofile[p->wmap_data.fd[i]];
//             // File-backed and shared mappings
//             if (!(p->wmap_data.flags[i] & MAP_ANONYMOUS) && isShared) {
//                 if (f == 0)
//                     panic("sys_wunmap: file not found");

//                 f->off = 0;

//                 for (uint a = addr; a < endAddr; a += PGSIZE) {
//                     pte_t *pte = get_pte(p->pgdir, (void *)a, 0);
//                     if (pte && (*pte & PTE_P)) {
//                         char *pa = P2V(PTE_ADDR(*pte));
//                         if (filewrite(f, pa, PGSIZE) != PGSIZE) {
//                             panic("sys_wunmap: filewrite failed");
//                         }
//                         if (!isShared) {
//                             kfree(pa);
//                             *pte = 0;
//                         }
//                     }
//                 }
//             }

//             // Invalidate PTEs and free physical pages for non-shared mappings
//             if (!isShared)
//             {
//                 for (uint a = addr; a < endAddr; a += PGSIZE)
//                 {
//                     pte_t *pte = get_pte(p->pgdir, (void *)a, 0);
//                     if (pte && (*pte & PTE_P))
//                     {
//                         char *pa = P2V(PTE_ADDR(*pte));
//                         kfree(pa);
//                         *pte = 0;
//                     }
//                 }
//             }
//             else
//             {
//                 for (uint a = addr; a < endAddr; a += PGSIZE)
//                 {
//                     pte_t *pte = get_pte(p->pgdir, (void *)a, 0);
//                     *pte &= ~PTE_P;
//                 }
//                 for (int i = index; i < p->wmap_data.total_mmaps - 1; i++) {
//                     p->wmap_data.addr[i] = p->wmap_data.addr[i + 1];
//                     p->wmap_data.length[i] = p->wmap_data.length[i + 1];
//                     p->wmap_data.n_loaded_pages[i] = p->wmap_data.n_loaded_pages[i + 1];
//                     p->wmap_data.flags[i] = p->wmap_data.flags[i + 1];
//                     p->wmap_data.fd[i] = p->wmap_data.fd[i + 1];
//                 }
//                 p->wmap_data.total_mmaps--;
//                 return 0;
//             }
//             break;
//         }
//     }

//     // If the address is not found, return an error
//     if (index == -1) {
//         return FAILED;
//     }

//     int isShared = p->wmap_data.flags[index] & MAP_SHARED;
//     uint startAddr = p->wmap_data.addr[index];
//     uint length = p->wmap_data.length[index];
//     int n_loaded_pages =  p->wmap_data.n_loaded_pages[index];
//     // Remove the mapping from the process's metadata array
//     for (int i = index; i < p->wmap_data.total_mmaps - 1; i++) {
//         p->wmap_data.addr[i] = p->wmap_data.addr[i + 1];
//         p->wmap_data.length[i] = p->wmap_data.length[i + 1];
//         p->wmap_data.n_loaded_pages[i] = p->wmap_data.n_loaded_pages[i + 1];
//         p->wmap_data.flags[i] = p->wmap_data.flags[i + 1];
//         p->wmap_data.fd[i] = p->wmap_data.fd[i + 1];
//     }
//     p->wmap_data.total_mmaps--;
//     if (isShared && p->parent) {
//         struct proc *parent = p->parent;
//         int parentIndex = parent->wmap_data.total_mmaps++;
//         parent->wmap_data.addr[parentIndex] = startAddr;
//         parent->wmap_data.length[parentIndex] = length;
//         parent->wmap_data.flags[parentIndex] = p->wmap_data.flags[index];
//         p->wmap_data.n_loaded_pages[parentIndex] = n_loaded_pages;
//         parent->wmap_data.fd[parentIndex] = p->wmap_data.fd[index];
//         // Note: Actual mapping recreation in parent's address space would require additional logic
//     }

//     return SUCCESS;
// }

int wunmap(uint addr)
{
    struct proc *p = myproc();
    int index = -1;

    // Validate the address (must be page-aligned)
    if (addr % PGSIZE != 0)
    {
        return FAILED;
    }

    // Find the mapping in wmap_data
    for (int i = 0; i < p->wmap_data.total_mmaps; i++)
    {
        if (p->wmap_data.addr[i] == addr)
        {
            index = i;
            break;
        }
    }

    // If the address is not found, return an error
    if (index == -1)
    {
        return FAILED;
    }

    // uint start = p->wmap_data.addr[index];
    // uint length = p->wmap_data.length[index];
    // int flags = (p->wmap_data.addr[index] & MAP_ANONYMOUS) ? MAP_ANONYMOUS : 0;
    uint start = p->wmap_data.addr[index];
    uint length = p->wmap_data.length[index];
    int flags = p->wmap_data.flags[index];

    // Write back to the file if it's a file-backed mapping with MAP_SHARED
    // **Handle File-Backed Mappings with MAP_SHARED**
    if (!(flags & MAP_ANONYMOUS) && (flags & MAP_SHARED))
    {
        struct file *f = p->ofile[p->wmap_data.fd[index]];
        if (f)
        {
            for (uint va = start; va < start + length; va += PGSIZE)
            {
                pte_t *pte = get_pte(p->pgdir, (void *)va, 0);
                if (pte && (*pte & PTE_P))
                {
                    char *pa = P2V(PTE_ADDR(*pte));
                    filewrite(f, pa, PGSIZE); // Write modified pages back to the file
                }
            }
        }
    }
    // if (!(flags & MAP_ANONYMOUS))
    // {
    //     struct file *f = p->ofile[index];
    //     if (f)
    //     {
    //         cprintf("wunmap: Closing file for fd %d\n", p->wmap_data.fd[index]);
    //         fileclose(f); // Decrement reference count for file descriptor
    //     }
    //     for (uint va = start; va < start + length; va += PGSIZE)
    //     {
    //         pte_t *pte = get_pte(p->pgdir, (void *)va, 0);
    //         if (!pte || !(*pte & PTE_P))
    //         {
    //             continue;
    //         }

    //         char *mem = P2V(PTE_ADDR(*pte));
    //         filewrite(f, mem, PGSIZE);
    //     }
    // }

    // Unmap the pages and free the physical memory
    // for (uint va = start; va < start + length; va += PGSIZE)
    // {
    //     pte_t *pte = get_pte(p->pgdir, (void *)va, 0);
    //     if (!pte || !(*pte & PTE_P))
    //     {
    //         continue;
    //     }

    //     uint pa = PTE_ADDR(*pte);
    //     char *mem = P2V(pa);
    //     kfree(mem);
    //     *pte = 0;
    // }

    // **Handle Memory Deallocation for Both Types**
    for (uint va = start; va < start + length; va += PGSIZE)
    {
        pte_t *pte = get_pte(p->pgdir, (void *)va, 0);
        if (pte && (*pte & PTE_P))
        {
            char *pa = P2V(PTE_ADDR(*pte));
            kfree(pa); // Free the physical memory
            *pte = 0;  // Invalidate the page table entry
        }
    }

    // Remove the mapping from wmap_data
    for (int i = index; i < p->wmap_data.total_mmaps - 1; i++)
    {
        p->wmap_data.addr[i] = p->wmap_data.addr[i + 1];
        p->wmap_data.length[i] = p->wmap_data.length[i + 1];
        p->wmap_data.n_loaded_pages[i] = p->wmap_data.n_loaded_pages[i + 1];
        p->wmap_data.flags[i] = p->wmap_data.flags[i + 1];
        p->wmap_data.fd[i] = p->wmap_data.fd[i + 1];
    }
    p->wmap_data.total_mmaps--;

    return SUCCESS;
}

int sys_va2pa(void)
{
    uint va;
    if (argint(0, (int *)&va) < 0)
    {
        return -1;
    }
    return (int)va2pa(va);
}

// Translate a virtual address to a physical address
uint va2pa(uint va)
{
    struct proc *p = myproc();
    pte_t *pte = get_pte(p->pgdir, (void *)va, 0);

    if (!pte)
    {
        // cprintf("va2pa: PTE not found for va 0x%x\n", va);
        return -1;
    }

    // Check if the PTE is present
    if (!(*pte & PTE_P))
    {
        // cprintf("va2pa: Page not present for va 0x%x\n", va);
        return -1;
    }

    // Ensure the address is not in a pre-mapped or kernel region
    // if (va >= KERNBASE || va < MMAPBASE)
    // {
    //     cprintf("va2pa: Address 0x%x is in a restricted or kernel region\n", va);
    //     return -1;
    // }

    // Compute the physical address with the page offset
    uint pa = PTE_ADDR(*pte);
    // cprintf("va2pa: Translated va 0x%x to pa 0x%x\n", va, pa);
    return pa;
}

int sys_getwmapinfo(void)
{
    struct wmapinfo *wminfo;

    // Fetch the user-provided pointer to wmapinfo structure
    if (argptr(0, (void *)&wminfo, sizeof(*wminfo)) < 0)
    {
        return -1;
    }

    struct proc *curproc = myproc();

    // Populate the wmapinfo structure
    wminfo->total_mmaps = curproc->wmap_data.total_mmaps;

    for (int i = 0; i < wminfo->total_mmaps; i++)
    {
        wminfo->addr[i] = curproc->wmap_data.addr[i];
        wminfo->length[i] = curproc->wmap_data.length[i];
        wminfo->n_loaded_pages[i] = curproc->wmap_data.n_loaded_pages[i];
    }

    return 0;
}
