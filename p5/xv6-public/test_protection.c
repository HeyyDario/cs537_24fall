#include "types.h"
#include "stat.h"
#include "user.h"

void test_readonly_string() {
    printf(1, "\n--- Test 1: Modifying a Read-Only String Literal ---\n");
    char *str = "This is a read-only string";
    printf(1, "Original string: %s\n", str);
    
    // Attempt to modify the string literal
    str[0] = 'X';
    
    // If modification succeeds, it's a failure (we expect a segmentation fault instead)
    printf(1, "Error: Successfully modified a read-only string!\n");
    exit();
}

void test_modify_code() {
    printf(1, "\n--- Test 2: Modifying Code Segment ---\n");
    
    // Function pointer pointing to the current function
    void (*func_ptr)() = &test_modify_code;

    printf(1, "Attempting to modify code segment...\n");

    // Attempt to overwrite part of the function code
    ((char *)func_ptr)[0] = 0x90;  // NOP instruction

    printf(1, "Error: Successfully modified code segment!\n");
    exit();
}

void test_readwrite_data() {
    printf(1, "\n--- Test 3: Modifying Read-Write Data Segment ---\n");
    
    // Regular read-write data
    char data[] = "This is read-write data";
    printf(1, "Original data: %s\n", data);

    // Modify the data
    data[0] = 'X';
    printf(1, "Modified data: %s\n", data);

    printf(1, "Success: Read-write data was modified correctly.\n");
}

int main() {
    // Run Test 1: Attempt to modify a read-only string literal
    if (fork() == 0) {
        test_readonly_string();
    }
    wait();

    // Run Test 2: Attempt to modify the code segment
    if (fork() == 0) {
        test_modify_code();
    }
    wait();

    // Run Test 3: Modify read-write data
    test_readwrite_data();

    printf(1, "\nAll tests completed.\n");
    exit();
}
