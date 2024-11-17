#include "types.h"
#include "user.h"
#include "wmap.h"

#define TEST_ADDR 0x60000000
#define TEST_LENGTH 8192
#define TEST_FLAGS (MAP_FIXED | MAP_SHARED | MAP_ANONYMOUS)

int main() {
    uint result;

    // Create a mapping with wmap
    result = wmap(TEST_ADDR, TEST_LENGTH, TEST_FLAGS, -1);
    if (result == (uint)-1) {
        printf(1, "wmap failed\n");
        exit();
    }

    // Unmap the memory with wunmap
    if (wunmap(TEST_ADDR) == FAILED) {
        printf(1, "wunmap failed\n");
        exit();
    }

    printf(1, "wunmap succeeded\n");
    exit();
}
