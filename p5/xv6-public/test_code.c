#include "types.h"
#include "stat.h"
#include "user.h"

int main(void) {
    char *code = (char *)&main;
    printf(1, "Before modification: 0x%x\n", code[0]);

    // Attempt to overwrite the first byte of main() with a NOP instruction (0x90)
    code[0] = 0x90;

    printf(1, "After modification: 0x%x\n", code[0]);
    printf(1, "Code modification succeeded!\n");
    exit();
}
