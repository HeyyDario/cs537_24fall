# CS537 P2: Solution to xv6 System Call
## Author Info:
* **Name**: Chengtao Dai

* **CS Login**: chengtao

* **Wisc ID**: 90852465877

* **Email**: cdai53@wisc.edu

## How to Run/Test the Program:
1. Add the name of your program to `UPROGS` in the Makefile, following the format of the other entries.

2. Run `make` to compile. Run `make qemu` to install `qemu`.

3. Run `make qemu-nox` and then run `<name_of_your_prog>` in the xv6 shell. (For more specifics, refer to `README.md` in the upper repo).

4. Run `Ctrl A x` to exit `qemu`.

## Implementation logistics:
### Overall Execution Sequence (from high-level to low-level)
1. User Program Calls `getparentname()` (User Mode) (**Appetizer**)
* In the user program (running in user mode), the function `getparentname()` is invoked:
```
getparentname(parentbuf, childbuf, sizeof(parentbuf), sizeof(childbuf));
```
* This function is defined in `user.h` and implemented in `usys.S` using the `SYSCALL(getparentname)` macro. This creates a wrapper that sets up the system call number (refer to `#include "syscall.h"`, where 23 for getparentname()) and the arguments (parentbuf, childbuf, parentbufsize, childbufsize).

2. Issuing a Trap to Switch to Kernel Mode
* Inside the wrapper function in `usys.S`, the system call number and arguments are placed into specific CPU registers (e.g., `eax`, `ebx`, `ecx`, etc.).
* The wrapper function (still in `usys.S`) then issues a trap instruction using int 64 (refer to `#include "traps.h"` where interrupt 64). This interrupt triggers a software trap, which causes the CPU to save the current process state (registers, program counter, etc.) onto the kernel stack and to switch from user mode to kernel mode. In terms of the mode switching, it is a privilege level switch where the CPU transitions from user mode (3) to kernel mode (0), giving the OS full control over the hardware.

3. Handling the Trap in the Kernel (Now comes the **Entrée**)
* The trap handler for system calls in xv6 is defined in `trapasm.S`. Now the flow jumps to the `alltraps` handler in `trapasm.S`. The `alltraps` function saves all registers to the stack, sets up the data segments to point to the kernel data segment and calls the `trap()` function in `trap.c`, passing a pointer to the trap frame (which contains the saved user-mode state, including the system call number).

4. Handling System Call in `trap.c`
* The `trap()` function in `trap.c` looks like the following. Since the trap number is T_SYSCALL (trap 64) in this case, the syscall() function is called. 
```
void
trap(struct trapframe *tf)
{
    if (tf->trapno == T_SYSCALL) {
        syscall();  // Call the system call handler (64 in this case)
        return;
    }
    // Other handlers
}
```

5. System Call Dispatch in `syscall.c`:
* The `syscall()` function retrieves the system call number from the `eax` register (stored in the trap frame), and then uses the system call number to look up the corresponding system call handler in the `syscalls[]` table (`[SYS_getparentname]   sys_getparentname,`).

6. Executing `sys_getparentname()`
* The `sys_getparentname` function is implemented in `sysproc.c`. It retrieves the parent process name and current process name, copies them into the provided buffers, and returns.

7. Returning to User Space in `trapasm.S` (**Dessert**)
* After the system call is complete, control returns to `trapret` in `trapasm.S`.
