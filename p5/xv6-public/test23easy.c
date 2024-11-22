#include "types.h"
#include "stat.h"
#include "user.h"

#define PGSIZE 4096

int main(void) {
    printf(1, "Starting Basic COW Test\n");

    // Allocate one page
    char *arr = malloc(PGSIZE);
    if (!arr) {
        printf(1, "Error: Failed to allocate memory\n");
        exit();
    }

    // Initialize the page with some data
    arr[0] = 'A';

    // Fork a child process
    int pid = fork();
    if (pid < 0) {
        printf(1, "Error: Fork failed\n");
        free(arr);
        exit();
    }

    if (pid == 0) { // Child process
        printf(1, "Child: arr[0] before modification = %c\n", arr[0]);

        // Modify the shared page
        arr[0] = 'B'; // This should trigger COW
        printf(1, "Child: arr[0] after modification = %c\n", arr[0]);

        // Exit the child process
        exit();
    } else { // Parent process
        // Wait for the child to finish
        wait();

        // Check if the parent retains its original value
        printf(1, "Parent: arr[0] after child modification = %c\n", arr[0]);

        // Free the allocated memory and exit
        free(arr);
        printf(1, "Basic COW Test completed successfully\n");
        exit();
    }
}

