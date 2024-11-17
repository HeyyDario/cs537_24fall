#include "types.h"
#include "user.h"
#include "wmap.h"

#define TEST_ADDR 0x60000000
#define TEST_LENGTH 8192  // Two pages (2 * 4096)
#define TEST_FLAGS (MAP_FIXED | MAP_SHARED | MAP_ANONYMOUS)

int main() {
    uint result;

    // Test 1: Basic anonymous memory mapping
    printf(1, "Test 1: Anonymous Memory Mapping\n");
    result = wmap(TEST_ADDR, TEST_LENGTH, TEST_FLAGS, -1);
    if (result == (uint)-1) {
        printf(1, "wmap failed in Test 1\n");
    } else {
        printf(1, "wmap succeeded at address 0x%x in Test 1\n", result);
    }

    // Test 2: Invalid address (not page-aligned)
    printf(1, "Test 2: Invalid Address (not page-aligned)\n");
    result = wmap(TEST_ADDR + 1, TEST_LENGTH, TEST_FLAGS, -1);
    if (result == (uint)-1) {
        printf(1, "wmap correctly failed in Test 2 (invalid address)\n");
    } else {
        printf(1, "wmap incorrectly succeeded in Test 2\n");
    }

    // Test 3: Invalid flags (missing MAP_FIXED)
    printf(1, "Test 3: Invalid Flags (missing MAP_FIXED)\n");
    result = wmap(TEST_ADDR, TEST_LENGTH, MAP_SHARED | MAP_ANONYMOUS, -1);
    if (result == (uint)-1) {
        printf(1, "wmap correctly failed in Test 3 (invalid flags)\n");
    } else {
        printf(1, "wmap incorrectly succeeded in Test 3\n");
    }

    // Test 4: Length not a multiple of page size
    printf(1, "Test 4: Invalid Length (not a multiple of page size)\n");
    result = wmap(TEST_ADDR, TEST_LENGTH + 1, TEST_FLAGS, -1);
    if (result == (uint)-1) {
        printf(1, "wmap correctly failed in Test 4 (invalid length)\n");
    } else {
        printf(1, "wmap incorrectly succeeded in Test 4\n");
    }

    // Test 5: Valid file-backed mapping (fd is 0, referring to stdin, which is a valid FD_INODE)
    printf(1, "Test 5: File-Backed Memory Mapping (using stdin as fd)\n");
    result = wmap(TEST_ADDR + TEST_LENGTH, TEST_LENGTH, TEST_FLAGS & ~MAP_ANONYMOUS, 0);
    if (result == (uint)-1) {
        printf(1, "wmap failed in Test 5\n");
    } else {
        printf(1, "wmap succeeded at address 0x%x in Test 5\n", result);
    }

    // Exit the test program
    printf(1, "All tests completed.\n");
    exit();
}
