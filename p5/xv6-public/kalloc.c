// Physical memory allocator, intended to allocate
// memory for user processes, kernel stacks, page table pages,
// and pipe buffers. Allocates 4096-byte pages.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "spinlock.h"

void freerange(void *vstart, void *vend);
extern char end[]; // first address after kernel loaded from ELF file
                   // defined by the kernel linker script in kernel.ld

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  int use_lock;
  struct run *freelist;
} kmem;

// Define the reference count array
#define MAX_PFN (PHYSTOP / PGSIZE) // Max number of physical frames
static uchar ref_counts[MAX_PFN]; // 1-byte ref count for each frame

// Helper to get the reference count for a physical address
static int pa_to_pfn(uint pa) {
    if (pa < V2P(end) || pa >= PHYSTOP)
        panic("pa_to_pfn: invalid physical address");
    return (pa >> PGSHIFT); // PFN is physical address divided by page size
}

void incref(uint pa) {
    int pfn = pa_to_pfn(pa);
    ref_counts[pfn]++;
}

void decref(uint pa) {
    int pfn = pa_to_pfn(pa);
    if (ref_counts[pfn] < 0) {
        cprintf("ERROR: decref called on pa=0x%x with invalid ref_count=%d\n", pa, ref_counts[pfn]);
        panic("decref: reference count underflow"); // Avoid crashing; adjust logic instead.
    }
    ref_counts[pfn]--;
    if (ref_counts[pfn] == 0) {
        kfree(P2V(pa)); // Free the physical page when no references remain
    }
}

int getref(uint pa) {
    int pfn = pa_to_pfn(pa);
    return ref_counts[pfn];
}

// Initialization happens in two phases.
// 1. main() calls kinit1() while still using entrypgdir to place just
// the pages mapped by entrypgdir on free list.
// 2. main() calls kinit2() with the rest of the physical pages
// after installing a full page table that maps them on all cores.
void
kinit1(void *vstart, void *vend)
{
  initlock(&kmem.lock, "kmem");
  kmem.use_lock = 0;
  freerange(vstart, vend);
}

void
kinit2(void *vstart, void *vend)
{
  freerange(vstart, vend);
  kmem.use_lock = 1;
}

void
freerange(void *vstart, void *vend)
{
  char *p;
  p = (char*)PGROUNDUP((uint)vstart);
  for(; p + PGSIZE <= (char*)vend; p += PGSIZE)
    kfree(p);
}
//PAGEBREAK: 21
// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(char *v)
{
  struct run *r;

  if((uint)v % PGSIZE || v < end || V2P(v) >= PHYSTOP)
    panic("kfree");

  // if (getref(V2P(v))) return;

  // Fill with junk to catch dangling refs.
  memset(v, 1, PGSIZE);

  if(kmem.use_lock)
    acquire(&kmem.lock);
  r = (struct run*)v;
  r->next = kmem.freelist;
  kmem.freelist = r;
  if(kmem.use_lock)
    release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
char*
kalloc(void)
{
  struct run *r;

  if(kmem.use_lock)
    acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  if(kmem.use_lock)
    release(&kmem.lock);

  // if (r) {
  //       // Initialize the reference count for the allocated page
  //       uint pa = V2P((char*)r);
  //       ref_counts[pa_to_pfn(pa)] = 1; // Start with ref_count = 1
  //   }
  return (char*)r;
}

