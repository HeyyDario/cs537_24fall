#include "types.h"
#include "stat.h"
#include "user.h"

#define PGSIZE 4096
#define NUM_PROCS 10

// Test 1: Basic COW Functionality
void test_basic_cow() {
    printf(1, "Running Test 1: Basic COW Functionality\n");

    int *x = (int *)sbrk(PGSIZE); // Allocate one page
    *x = 42;                      // Write to the page

    if (fork() == 0) {
        // Child process
        printf(1, "Child: Value of x = %d\n", *x); // Should print 42
        *x = 99;                                  // Write to the page (COW should trigger here)
        printf(1, "Child: Value of x after write = %d\n", *x); // Should print 99
        exit();
    } else {
        // Parent process
        wait();
        printf(1, "Parent: Value of x = %d\n", *x); // Should still print 42
    }
}

// Test 2: Nested Forks
void test_nested_forks() {
    printf(1, "Running Test 2: Nested Forks\n");

    int *x = (int *)sbrk(PGSIZE);
    *x = 123;

    if (fork() == 0) {
        // Child process
        if (fork() == 0) {
            // Grandchild process
            printf(1, "Grandchild: Value of x = %d\n", *x); // Should print 123
            *x = 789;                                      // Write to the page
            printf(1, "Grandchild: Value of x after write = %d\n", *x); // Should print 789
            exit();
        } else {
            wait();
            printf(1, "Child: Value of x = %d\n", *x); // Should still print 123
            exit();
        }
    } else {
        wait();
        printf(1, "Parent: Value of x = %d\n", *x); // Should still print 123
    }
}

// Test 3: Write to a Read-Only Page (Alternative)
void test_write_read_only() {
    printf(1, "Running Test 3: Write to a Read-Only Page\n");

    char *x = sbrk(4096); // Allocate one page
    x[0] = 'R';           // Write initial value

    int pid = fork();
    if (pid < 0) {
        printf(1, "Fork failed\n");
        exit();
    }

    if (pid == 0) {
        // Child process
        // Try writing to a read-only page
        printf(1, "Child writes to read-only page...\n");
        x[0] = 'W'; // Expecting segmentation fault due to COW
        printf(1, "Test 3 Failed: Child was able to write to read-only page\n");
        exit();
    } else {
        wait(); // Wait for child
        printf(1, "Parent sees: x[0] = %c\n", x[0]);
        if (x[0] != 'R') {
            printf(1, "Test 3 Failed: Unexpected modification in parent\n");
        } else {
            printf(1, "Test 3 Passed\n");
        }
    }
}

// Test 4: Reference Count Validation
void test_reference_count() {
    printf(1, "Running Test 4: Reference Count Validation\n");

    int *x = (int *)sbrk(PGSIZE);
    *x = 55;

    if (fork() == 0) {
        // Child process
        printf(1, "Child: Value of x = %d\n", *x); // Should print 55
        if (fork() == 0) {
            // Grandchild process
            printf(1, "Grandchild: Value of x = %d\n", *x); // Should print 55
            exit();
        } else {
            wait();
            printf(1, "Child: Value of x after grandchild exits = %d\n", *x); // Should still print 55
            exit();
        }
    } else {
        wait();
        printf(1, "Parent: Value of x = %d\n", *x); // Should still print 55
    }
}

// Test 5: Concurrent Writes
void test_concurrent_writes() {
    printf(1, "Running Test 5: Concurrent Writes\n");

    int *x = (int *)sbrk(PGSIZE);
    *x = 100;

    if (fork() == 0) {
        // Child process
        *x = 200; // Trigger COW
        printf(1, "Child: Value of x = %d\n", *x); // Should print 200
        exit();
    } else {
        wait();
        printf(1, "Parent: Value of x = %d\n", *x); // Should still print 100
    }
}

// Test 6: Stress Test
void test_stress() {
    printf(1, "Running Test 6: Stress Test\n");

    int *x = (int *)sbrk(PGSIZE);
    *x = 1;

    for (int i = 0; i < NUM_PROCS; i++) {
        if (fork() == 0) {
            printf(1, "Process %d: Value of x = %d\n", i + 1, *x);
            *x = i + 2; // Trigger COW
            printf(1, "Process %d: Value of x after write = %d\n", i + 1, *x);
            exit();
        }
    }

    for (int i = 0; i < NUM_PROCS; i++) {
        wait();
    }

    printf(1, "Parent: Value of x = %d\n", *x); // Should still print 1
}

// Menu to run tests
void run_tests() {
    printf(1, "COW Test Suite\n");
    printf(1, "1. Basic COW Functionality\n");
    printf(1, "2. Nested Forks\n");
    printf(1, "3. Write to Read-Only Page\n");
    printf(1, "4. Reference Count Validation\n");
    printf(1, "5. Concurrent Writes\n");
    printf(1, "6. Stress Test\n");
    printf(1, "7. Run All Tests\n");
    printf(1, "Enter your choice: ");

    char buf[2];
    read(0, buf, 2);
    int choice = buf[0] - '0';

    switch (choice) {
    case 1:
        test_basic_cow();
        break;
    case 2:
        test_nested_forks();
        break;
    case 3:
        test_write_read_only();
        break;
    case 4:
        test_reference_count();
        break;
    case 5:
        test_concurrent_writes();
        break;
    case 6:
        test_stress();
        break;
    case 7:
        test_basic_cow();
        test_nested_forks();
        test_write_read_only();
        test_reference_count();
        test_concurrent_writes();
        test_stress();
        break;
    default:
        printf(1, "Invalid choice.\n");
        break;
    }
}

int main() {
    run_tests();
    exit();
}
