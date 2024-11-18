#include "types.h"
#include "stat.h"
#include "user.h"
#include "wmap.h"

int main() {
    uint addr = 0x60000000;  // Valid address range
    int length = 4096;       // One page
    int flags = MAP_FIXED | MAP_SHARED | MAP_ANONYMOUS;
    int fd = -1;             // For anonymous mapping

    // Call wmap to create a mapping
    if (wmap(addr, length, flags, fd) == FAILED) {
        printf(1, "Failed to create mapping\n");
        exit();
    }

    struct wmapinfo info;
    if (getwmapinfo(&info) < 0) {
        printf(1, "getwmapinfo failed\n");
        exit();
    }

    printf(1, "Total mmaps: %d\n", info.total_mmaps);
    for (int i = 0; i < info.total_mmaps; i++) {
        printf(1, "Mapping %d: addr = 0x%x, length = %d, loaded pages = %d\n",
               i, info.addr[i], info.length[i], info.n_loaded_pages[i]);
    }

    exit();
}
