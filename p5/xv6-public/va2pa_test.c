#include "types.h"
#include "stat.h"
#include "user.h"

int main() {
    // Extend the heap by one page (PGSIZE)
    char *heap_var = sbrk(4096); // PGSIZE = 4096 bytes
    printf(1, "Virtual address of heap end after sbrk: 0x%x\n", heap_var);

    // Write to the heap address to ensure allocation
    *heap_var = 'A';

    uint pa = va2pa((uint)heap_var);
    if (pa == -1) {
        printf(1, "Translation failed\n");
    } else {
        printf(1, "Physical address: 0x%x\n", pa);
    }

    exit();
}
