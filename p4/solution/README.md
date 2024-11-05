xv6 Stride Scheduler Implementation
This project modifies the xv6 operating system to implement a stride scheduler with dynamic ticket allocation. It includes several new system calls and modifications to support a modular scheduling system that can switch between Round Robin (RR) and Stride scheduling.

Overview
The stride scheduler assigns CPU time based on ticket allocations, allowing processes with more tickets to receive a proportionally larger share of CPU time. Our implementation supports dynamically adjusting tickets and retrieves scheduling information for each process.

Steps to Implement the Stride Scheduler
Task 1: Add Fields for Stride Scheduling in struct proc
We modified struct proc in proc.h to include the following fields necessary for the stride scheduler:

int tickets; — Number of tickets for each process (default is 8).
int stride; — Inversely proportional to tickets, calculated as STRIDE1 / tickets.
int pass; — Pass value used to track when a process should next run.
int remain; — Tracks remaining stride for a process when it temporarily leaves the runnable queue.
int run_ticks; — Tracks the total CPU time (in ticks) that a process has received.
Task 2: Modify allocproc and exit Functions
In proc.c, we modified allocproc() to initialize tickets, stride, and pass for each new process and update global_tickets and global_stride. Similarly, we updated exit() to decrement global_tickets and recalculate global_stride when a process exits.

Task 3: Implement settickets System Call
We created the settickets(int n) system call to allow a process to set its own ticket allocation. The maximum allowed ticket value is 32 (1 << 5), and the minimum is 1. If n is less than 1, tickets are set to the default of 8.

int sys_settickets(void) {
    int n;
    struct proc *p = myproc();

    if (argint(0, &n) < 0)
        return -1;

    if (n < 1) n = 8;
    else if (n > 32) n = 32;

    acquire_ptable_lock();

    global_tickets -= p->tickets;
    p->tickets = n;
    global_tickets += p->tickets;

    p->stride = STRIDE1 / p->tickets;
    set_global_stride(STRIDE1 / global_tickets);

    release_ptable_lock();
    return 0;
}
Task 4: Implement getpinfo System Call
We added a new system call, getpinfo, which fills a struct pstat with scheduling information for all processes. This structure is defined in pstat.h and includes fields for process states, ticket counts, pass values, remaining strides, strides, and total runtime in ticks.

int sys_getpinfo(void) {
    struct pstat *ps;
    if (argptr(0, (void*)&ps, sizeof(*ps)) < 0)
        return -1;

    acquire_ptable_lock();
    struct proc *p;
    int i = 0;
    for (p = getptable(); p < getptable() + NPROC; i++, p++) {
        ps->inuse[i] = (p->state != UNUSED) ? 1 : 0;
        ps->tickets[i] = p->tickets;
        ps->pid[i] = p->pid;
        ps->pass[i] = p->pass;
        ps->remain[i] = p->remain;
        ps->stride[i] = p->stride;
        ps->rtime[i] = p->run_ticks;
    }
    release_ptable_lock();
    return 0;
}
Task 5: Add Modular Scheduler with SCHED_MACRO
The scheduler() function in proc.c was modified to support a modular scheduling approach, allowing us to switch between RR and Stride schedulers based on a SCHED_MACRO variable in the Makefile. Depending on the flag, the scheduler either performs standard Round Robin scheduling or selects the process with the lowest pass value, using run_ticks and pid for tie-breaking.

SCHED_MACRO = RR
ifeq ($(SCHEDULER), STRIDE)
SCHED_MACRO = STRIDE
endif
CFLAGS += -D $(SCHED_MACRO)
Task 6: Update the Makefile and Test with workload
To verify our scheduler, we added _workload to the UPROGS in the Makefile and used getpinfo to collect scheduling information during a CPU-intensive workload.

UPROGS=\
    _cat\
    _echo\
    _forktest\
    _grep\
    _init\
    _kill\
    _ln\
    _ls\
    _mkdir\
    _rm\
    _sh\
    _stressfs\
    _usertests\
    _wc\
    _zombie\
    _workload\
Run the Round Robin scheduler first:

make qemu-nox
Then modify SCHED_MACRO to STRIDE and re-run make qemu-nox to observe and compare the behavior.

Summary of New System Calls
settickets(int n) — Sets the ticket count for the calling process.
settickets_pid(int pid, int n) — Sets tickets for a specific process by pid.
getpinfo(struct pstat*) — Retrieves scheduling information for all processes in a pstat structure.
Files Modified or Added
proc.h — Added new fields to struct proc.
proc.c — Implemented stride scheduler logic and modified process creation and exit logic.
sysproc.c — Added system calls settickets and getpinfo.
pstat.h — Defined struct pstat for scheduling information.
Makefile — Modified to support SCHED_MACRO and _workload.