#include "types.h"
#include "stat.h"
#include "user.h"

#define PGSIZE 4096

int main() {
    printf(1, "Starting Copy-on-Write Test...\n");

    // Allocate a page of memory
    char *shared_mem = sbrk(PGSIZE);
    if (shared_mem == (char *)-1) {
        printf(1, "Memory allocation failed!\n");
        exit();
    }

    // Initialize the shared memory
    shared_mem[0] = 'A';
    shared_mem[PGSIZE - 1] = 'Z';

    // Fork the process
    int pid = fork();
    if (pid < 0) {
        printf(1, "Fork failed!\n");
        exit();
    }

    if (pid == 0) {
        // Child process
        printf(1, "Child: Checking shared memory...\n");

        // Check if the shared memory has the correct values
        if (shared_mem[0] != 'A' || shared_mem[PGSIZE - 1] != 'Z') {
            printf(1, "Child: Shared memory check failed!\n");
            exit();
        }

        // Modify the shared memory
        printf(1, "Child: Modifying shared memory...\n");
        shared_mem[0] = 'C';
        shared_mem[PGSIZE - 1] = 'D';

        printf(1, "Child: Shared memory modified successfully.\n");
        exit();
    } else {
        // Parent process
        wait();  // Wait for the child to finish

        printf(1, "Parent: Checking shared memory...\n");

        // Check if the shared memory has the original values (COW should have happened)
        if (shared_mem[0] != 'A' || shared_mem[PGSIZE - 1] != 'Z') {
            printf(1, "Parent: Copy-on-Write failed! Shared memory was modified by the child.\n");
        } else {
            printf(1, "Parent: Copy-on-Write successful! Shared memory is unchanged.\n");
        }

        printf(1, "Copy-on-Write Test completed.\n");
    }

    exit();
}
