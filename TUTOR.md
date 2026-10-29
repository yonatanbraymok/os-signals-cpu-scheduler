# OS Assignment 3 — Complete Exam Tutor Guide
## Signals, CPU Scheduling, and Synchronization

> This document covers **every concept** tested in this assignment from first principles. Read it top-to-bottom at least once, then use the section headers as a quick-reference on exam day.

---

## Table of Contents

1. [Assignment Big Picture](#1-assignment-big-picture)
2. [Program Entry Point — ex3.c](#2-program-entry-point--ex3c)
3. [Part 1: Unix Signals (Focus Mode)](#3-part-1-unix-signals--focus-mode)
   - 3.1 What Is a Signal?
   - 3.2 Signal Lifecycle: Generation → Pending → Delivery
   - 3.3 Signal Registration with `sigaction`
   - 3.4 Blocking Signals with `sigprocmask`
   - 3.5 Inspecting Pending Signals: `sigpending` and `sigismember`
   - 3.6 Sending Signals to Yourself: `raise()`
   - 3.7 Async-Signal Safety
   - 3.8 Important Signal Properties (Deduplication)
   - 3.9 Focus-Mode.c — Full Annotated Walkthrough
4. [Part 2: CPU Scheduling (CPU-Scheduler)](#4-part-2-cpu-scheduling--cpu-scheduler)
   - 4.1 What Is CPU Scheduling?
   - 4.2 Key Definitions
   - 4.3 FCFS — First Come, First Served
   - 4.4 SJF — Shortest Job First
   - 4.5 Priority Scheduling
   - 4.6 Round Robin
   - 4.7 Process Creation with `fork()`
   - 4.8 Killing and Controlling Processes: `kill()`, `SIGSTOP`, `SIGCONT`, `SIGKILL`
   - 4.9 Time Simulation with `alarm()` and `sigsuspend()`
   - 4.10 Race Conditions and How They Are Avoided
   - 4.11 CPU-Scheduler.c — Full Annotated Walkthrough
5. [Worked Examples — Trace Every Test Case](#5-worked-examples--trace-every-test-case)
6. [Key System Calls — Quick Reference](#6-key-system-calls--quick-reference)
7. [Common Exam Questions & Answers](#7-common-exam-questions--answers)
8. [Bug in ex3.c — Know This](#8-bug-in-ex3c--know-this)

---

## 1. Assignment Big Picture

**File:** `README.md`

The assignment simulates two real OS subsystems entirely in C using Unix primitives:

| Part | Name | Weight | OS Concept |
|------|------|--------|------------|
| Warmup | Focus Mode | 20% | Unix Signal Handling |
| Main | CPU Scheduler | 80% | Process Scheduling Algorithms |

**Compilation and execution:**
```bash
gcc ex3.c -o ex3
./ex3 Focus-Mode <num-rounds> <round-duration>
./ex3 CPU-Scheduler <processes.csv> <time-quantum>
```

The three source files are:
- `ex3.c` — argument validation and routing
- `Focus-Mode.c` — warmup implementation (included directly in ex3.c)
- `CPU-Scheduler.c` — main part implementation (included directly in ex3.c)

The unusual `#include "Focus-Mode.c"` pattern compiles everything as a single translation unit. This is non-standard practice but works fine here since each `.c` file defines its functions and `ex3.c` just calls them.

---

## 2. Program Entry Point — ex3.c

**File path:** `ex3.c`

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "Focus-Mode.c"      // includes all Focus Mode functions
#include "CPU-Scheduler.c"   // includes all CPU Scheduler functions

int main(int argc, char *argv[]) {

    // Must have exactly 4 arguments: program name + 3 real args
    if (argc != 4) {
        printf("Usage: %s <Focus-Mode/CPU-Schedule> <Num-Of-Rounds/Processes.csv> <Round-Duration/Time-Quantum>",
               argv[0]);
        exit(0);
    }

    // BUG: This condition uses && instead of ||
    // A string CANNOT equal both "Focus-Mode" AND "CPU-Scheduler" simultaneously,
    // so this branch is DEAD CODE and never triggers.
    // It should be: if (strcmp(...) != 0 && strcmp(...) != 0)
    // OR equivalently: if (!strcmp(...) || !strcmp(...))  → also wrong
    // The correct intent is: if the first arg is NEITHER "Focus-Mode" NOR "CPU-Scheduler"
    if (!strcmp(argv[1], "Focus-Mode") && !strcmp(argv[1], "CPU-Scheduler")) {
        printf("Usage: ...");
        exit(0);
    }

    if (strcmp(argv[1], "Focus-Mode") == 0) {
        int numOfRounds   = atoi(argv[2]);
        int roundDuration = atoi(argv[3]);
        runFocusMode(numOfRounds, roundDuration);
    }

    if (strcmp(argv[1], "CPU-Scheduler") == 0) {
        char *processesCsvFilePath = argv[2];
        int   timeQuantum          = atoi(argv[3]);
        runCPUScheduler(processesCsvFilePath, timeQuantum);
    }

    return 0;
}
```

**Key points:**
- `argc` counts all tokens including `argv[0]` (the program name). For `./ex3 Focus-Mode 3 10`, argc = 4.
- `strcmp(a, b)` returns 0 when the strings are equal. `!strcmp(...)` is therefore true when equal.
- `atoi("42")` converts the string `"42"` to the integer `42`. It does no error checking.

---

## 3. Part 1: Unix Signals (Focus Mode)

### 3.1 What Is a Signal?

A **signal** is an asynchronous notification sent to a process. Think of it as a software interrupt: the OS interrupts whatever the process is doing and runs a special handler function.

Signals are identified by integers with symbolic names:

| Number | Name | Default action | Meaning |
|--------|------|----------------|---------|
| 1 | `SIGHUP` | Terminate | Hangup (terminal closed) |
| 2 | `SIGINT` | Terminate | Keyboard interrupt (Ctrl+C) |
| 9 | `SIGKILL` | Terminate | Force kill — cannot be caught or blocked |
| 10 | `SIGUSR1` | Terminate | User-defined 1 |
| 12 | `SIGUSR2` | Terminate | User-defined 2 |
| 14 | `SIGALRM` | Terminate | Timer expired (from `alarm()`) |
| 17 | `SIGCHLD` | Ignore | Child process stopped or exited |
| 18 | `SIGCONT` | Continue | Resume stopped process — cannot be blocked |
| 19 | `SIGSTOP` | Stop | Pause process — cannot be caught or blocked |

This assignment uses:
- `SIGUSR1` → Email notification (Focus Mode)
- `SIGUSR2` → Delivery reminder (Focus Mode)
- `SIGHUP` → Doorbell (Focus Mode)
- `SIGALRM` → Timer for time simulation (CPU Scheduler)
- `SIGSTOP` / `SIGCONT` / `SIGKILL` → Round Robin process control (CPU Scheduler)

---

### 3.2 Signal Lifecycle: Generation → Pending → Delivery

```
SIGNAL GENERATED
    │
    ▼
Is signal BLOCKED for this process?
    │
    ├── YES → Signal becomes PENDING (stored in kernel's pending set)
    │         It stays pending until unblocked.
    │         IMPORTANT: Identical signals do NOT queue up.
    │         A second SIGUSR1 while one is already pending is silently dropped.
    │
    └── NO  → Signal is DELIVERED immediately
              │
              ▼
         What is the handler?
              │
              ├── SIG_DFL  → Default action (terminate, stop, ignore, etc.)
              ├── SIG_IGN  → Ignore the signal
              └── function → Run the custom handler function
```

**Connection to OS theory:** The OS kernel maintains a **pending signal bitmask** per process. Because it's a bitmask (one bit per signal), you cannot have two instances of the same signal pending at once. This is a fundamental property to understand.

---

### 3.3 Signal Registration with `sigaction`

`sigaction` is the POSIX-standard way to register a signal handler. It is preferred over the older `signal()` because it is more portable and allows fine-grained control.

```c
// From Focus-Mode.c

struct sigaction sa;
sigemptyset(&sa.sa_mask);  // no extra signals blocked during handler execution
sa.sa_flags = 0;           // no special flags

sa.sa_handler = handle_email;
sigaction(SIGUSR1, &sa, NULL);  // register handle_email for SIGUSR1

sa.sa_handler = handle_delivery;
sigaction(SIGUSR2, &sa, NULL);  // register handle_delivery for SIGUSR2

sa.sa_handler = handle_doorbell;
sigaction(SIGHUP, &sa, NULL);   // register handle_doorbell for SIGHUP
```

**The `sigaction` struct explained:**

```c
struct sigaction {
    void     (*sa_handler)(int);  // simple handler: just takes signal number
    void     (*sa_sigaction)(int, siginfo_t *, void *);  // advanced handler
    sigset_t   sa_mask;           // signals to block WHILE this handler runs
    int        sa_flags;          // modifiers
};
```

**Important flags:**
- `SA_RESTART` — Automatically restart interrupted system calls (used in CPU-Scheduler for `SIGALRM`).
- `SA_SIGINFO` — Use `sa_sigaction` instead of `sa_handler` (richer info).
- `0` — No special behavior (used in Focus Mode).

**Why `sigaction` over `signal()`?**
- `signal()` behavior is implementation-defined (some systems auto-reset to SIG_DFL after one delivery).
- `sigaction()` is guaranteed portable and consistent across POSIX systems.

---

### 3.4 Blocking Signals with `sigprocmask`

Blocking a signal does NOT discard it — it makes it **pending** (queued in the kernel) until you unblock it.

```c
// From Focus-Mode.c

sigset_t block_set;
sigemptyset(&block_set);         // start with empty set
sigaddset(&block_set, SIGUSR1);  // add SIGUSR1
sigaddset(&block_set, SIGUSR2);  // add SIGUSR2
sigaddset(&block_set, SIGHUP);   // add SIGHUP

// SIG_BLOCK: add these signals to the current blocked set
sigprocmask(SIG_BLOCK, &block_set, NULL);
```

**`sigprocmask` operations:**

| Operation | Meaning |
|-----------|---------|
| `SIG_BLOCK` | Add the given set to the current blocked set (block more signals) |
| `SIG_UNBLOCK` | Remove the given set from the current blocked set (unblock) |
| `SIG_SETMASK` | Replace the current blocked set entirely with the given set |

**The `sigset_t` type** is the kernel's signal set. It is manipulated only through these functions:

| Function | Effect |
|----------|--------|
| `sigemptyset(&set)` | Clear all signals from set |
| `sigfillset(&set)` | Add all signals to set |
| `sigaddset(&set, sig)` | Add one signal to set |
| `sigdelset(&set, sig)` | Remove one signal from set |
| `sigismember(&set, sig)` | Returns 1 if signal is in set, 0 otherwise |

**Connection to OS theory:** The "signal mask" is a per-thread/per-process bitmask stored in the kernel's PCB (Process Control Block). `sigprocmask` reads and writes that mask.

---

### 3.5 Inspecting Pending Signals: `sigpending` and `sigismember`

At the end of each Focus round, we want to know which signals are waiting:

```c
// From Focus-Mode.c

sigset_t pending;
sigpending(&pending);  // fills 'pending' with the set of currently pending signals

int any_distraction = 0;

if (sigismember(&pending, SIGUSR1)) {   // is SIGUSR1 pending?
    any_distraction = 1;
    printf(" - Email notification is waiting.\n");
    fflush(stdout);
    deliver_pending_signal(SIGUSR1);    // temporarily unblock to deliver
}

if (sigismember(&pending, SIGUSR2)) {
    any_distraction = 1;
    printf(" - You have a reminder to pick up your delivery.\n");
    fflush(stdout);
    deliver_pending_signal(SIGUSR2);
}

if (sigismember(&pending, SIGHUP)) {
    any_distraction = 1;
    printf(" - The doorbell is ringing.\n");
    fflush(stdout);
    deliver_pending_signal(SIGHUP);
}
```

The **order of checking** (SIGUSR1, SIGUSR2, SIGHUP) is intentional — the assignment requires distraction outcomes to always be printed in the order: Email → Delivery → Doorbell, regardless of what order the user triggered them.

---

### 3.6 Sending Signals to Yourself: `raise()`

```c
raise(SIGUSR1);   // equivalent to kill(getpid(), SIGUSR1)
```

Because signals are **blocked** at this point, the signal goes **pending** instead of being delivered. This is the core trick of Focus Mode:

```
User presses '1'
    └─> raise(SIGUSR1)
            └─> SIGUSR1 is currently BLOCKED
                    └─> SIGUSR1 added to pending set
                            └─> Nothing happens immediately
```

Then at end of round, we unblock one signal at a time to deliver it.

---

### 3.7 Async-Signal Safety

**Critical concept for the exam:** A signal handler can interrupt your process at *any* point — even in the middle of `printf()`. Because `printf()` uses internal buffers and locks, calling it from a signal handler can cause **deadlock** or **data corruption**.

The solution: use `write()` instead, which is **async-signal-safe**.

```c
// From Focus-Mode.c — handlers use write(), NOT printf()

static void handle_email(int sig) {
    (void)sig;   // suppress unused-parameter warning
    const char *msg = "[Outcome:] The TA announced: Everyone get 100 on the exercise!\n";
    write(STDOUT_FILENO, msg, strlen(msg));
}

static void handle_delivery(int sig) {
    (void)sig;
    const char *msg = "[Outcome:] You picked it up just in time.\n";
    write(STDOUT_FILENO, msg, strlen(msg));
}

static void handle_doorbell(int sig) {
    (void)sig;
    const char *msg = "[Outcome:] Food delivery is here.\n";
    write(STDOUT_FILENO, msg, strlen(msg));
}
```

**Key distinction:**
- `printf()` — NOT async-signal-safe (uses stdio buffers, internal locks)
- `write()` — IS async-signal-safe (direct kernel call, no buffering)
- `STDOUT_FILENO` = 1 (file descriptor for standard output)

**Why `(void)sig`?** The signal handler prototype must be `void handler(int sig)`. The `sig` parameter tells you which signal arrived. Here the parameter is ignored, so `(void)sig` silences the compiler warning.

---

### 3.8 Important Signal Properties (Deduplication)

**This is frequently asked in OS exams.**

If the same signal is raised multiple times while it is blocked, only **one** instance becomes pending. All subsequent raises of the same signal are **silently dropped**.

```
Round example: User presses 1, 1, 1 (three times)
─────────────────────────────────────────────────
raise(SIGUSR1)  → pending = {SIGUSR1}        ← queued
raise(SIGUSR1)  → pending = {SIGUSR1}        ← DROPPED (already pending!)
raise(SIGUSR1)  → pending = {SIGUSR1}        ← DROPPED (already pending!)

At end of round: only ONE email handler fires.
```

From the expected output for Round 3 in the README (input: three `1`s):
```
 - Email notification is waiting.
[Outcome:] The TA announced: Everyone get 100 on the exercise!
```
Only one outcome line — confirming deduplication.

This is because the kernel's pending signal set is a **bitmask**, not a queue. One bit per signal number.

---

### 3.9 Focus-Mode.c — Full Annotated Walkthrough

**File path:** `Focus-Mode.c`

The key helper function that delivers a single pending signal safely:

```c
// Temporarily unblock one signal so its handler fires, then immediately re-block it.
// This is the ONLY safe way to handle a specific pending signal when all others
// should remain blocked.
static void deliver_pending_signal(int sig) {
    sigset_t temp;
    sigemptyset(&temp);
    sigaddset(&temp, sig);
    sigprocmask(SIG_UNBLOCK, &temp, NULL);  // unblock → signal fires → handler runs
    sigprocmask(SIG_BLOCK, &temp, NULL);    // immediately re-block for next round
}
```

**Step-by-step of a full round:**

```
BEFORE ROUND:
  sigprocmask(SIG_BLOCK, {SIGUSR1, SIGUSR2, SIGHUP}, NULL)
  Signals are blocked. Any raise() goes to PENDING.

DURING ROUND:
  User input loop (duration times):
    '1' → raise(SIGUSR1) → PENDING (or dropped if already pending)
    '2' → raise(SIGUSR2) → PENDING (or dropped if already pending)
    '3' → raise(SIGHUP)  → PENDING (or dropped if already pending)
    'q' → break out of loop early

END OF ROUND:
  sigpending(&pending)                    → query what's waiting
  sigismember(&pending, SIGUSR1)?
    YES: printf(" - Email notification...")
         deliver_pending_signal(SIGUSR1)  → unblock → handle_email runs → re-block
  sigismember(&pending, SIGUSR2)?
    YES: printf(" - You have a reminder...")
         deliver_pending_signal(SIGUSR2)  → unblock → handle_delivery runs → re-block
  sigismember(&pending, SIGHUP)?
    YES: printf(" - The doorbell...")
         deliver_pending_signal(SIGHUP)   → unblock → handle_doorbell runs → re-block

AFTER ALL ROUNDS:
  sigprocmask(SIG_UNBLOCK, {SIGUSR1, SIGUSR2, SIGHUP}, NULL)
  All signals restored to unblocked state.
```

**The `fflush(stdout)` calls** after `printf()` in the main function are important: they force the stdio buffer to be written to the terminal before the signal handler's `write()` output follows. Without `fflush()`, the output ordering could be non-deterministic.

---

## 4. Part 2: CPU Scheduling (CPU-Scheduler)

### 4.1 What Is CPU Scheduling?

The OS **scheduler** decides which process gets to use the CPU next when multiple processes are ready to run. This assignment simulates 4 classic scheduling algorithms.

**Key states in process lifecycle:**
```
NEW → READY → RUNNING → TERMINATED
                ↑   ↓
              WAITING (blocked on I/O or signal)
```

- **Ready queue**: processes that are ready to run but waiting for CPU
- **Scheduler**: picks next process from ready queue
- **Dispatcher**: context-switches to the chosen process

---

### 4.2 Key Definitions

**These formulas are directly tested.**

```
Arrival Time   = time when process enters the ready queue
Burst Time     = total CPU time the process needs to complete
Start Time     = time when process first gets the CPU
Completion Time = time when process finishes execution
```

**Waiting Time (per process):**
```
Waiting Time = Start Time − Arrival Time
```
Example: Process arrives at t=4, gets CPU at t=8 → Waiting Time = 4

**Average Waiting Time:**
```
Average Waiting Time = (sum of all waiting times) / (number of processes)
```

**Turnaround Time (per process):**
```
Turnaround Time = Completion Time − Arrival Time
```

**Total Turnaround Time (Round Robin):**
The assignment defines this as the **total elapsed time from t=0 until the last process finishes**. This is just the final clock value `t` at the end of Round Robin execution.

**Important note:** Idle time (when no process is ready) does NOT count as waiting time for any process.

---

### 4.3 FCFS — First Come, First Served

**Rule:** Among arrived processes, run the one that arrived earliest. Ties broken by original CSV order.

**Properties:**
- Non-preemptive: once started, runs to completion
- Simple but can suffer from **convoy effect** (a short job stuck behind a long one)
- Average waiting time can be very high if a long job arrives first

**Trace — processes1.csv with quantum=2:**
```
P1: arrival=0, burst=3, priority=5
P2: arrival=5, burst=4, priority=4
P3: arrival=4, burst=4, priority=1
P4: arrival=4, burst=1, priority=2

Sort by arrival: P1(0), P3(4), P4(4), P2(5)
  (P3 and P4 both arrive at 4; P3 comes first in CSV → P3 before P4)

t=0:  P1 available. Run P1. Runs 0→3.
t=3:  No process has arrived (P3,P4 arrive at 4). Idle 3→4.
t=4:  P3 and P4 both arrived. FCFS picks earliest arrival = 4 (tie).
      Tie-break by CSV order: P3 was index 2, P4 was index 3 → P3 wins.
      Run P3: 4→8.
t=8:  P4 available (arrived at 4). Run P4: 8→9.
t=9:  P2 available (arrived at 5). Run P2: 9→13.

Waiting times:
  P1: start=0, arrival=0  → wait=0
  P3: start=4, arrival=4  → wait=0
  P4: start=8, arrival=4  → wait=4
  P2: start=9, arrival=5  → wait=4

Average = (0+0+4+4)/4 = 8/4 = 2.00 ✓ (matches output1.txt)
```

---

### 4.4 SJF — Shortest Job First

**Rule:** Among all arrived processes, run the one with the shortest burst time. Ties broken by original CSV order.

**Properties:**
- Non-preemptive (in this assignment)
- **Optimal** for minimizing average waiting time among non-preemptive algorithms
- Can cause **starvation**: long processes may wait forever if short ones keep arriving
- Requires knowing burst time in advance (real OS uses estimates/predictions)

**Trace — processes1.csv:**
```
t=0:  Only P1 arrived. Run P1: 0→3.
t=3:  Idle 3→4 (no arrived processes).
t=4:  P3(burst=4) and P4(burst=1) available. SJF picks shortest = P4(burst=1).
      Run P4: 4→5.
t=5:  P3(burst=4) and P2(burst=4) available. Tie on burst.
      Tie-break by CSV order: P3 was index 2, P2 was index 1. P3 < P2? No:
      orig_idx: P1=0, P2=1, P3=2, P4=3. So P2 has smaller orig_idx? 
      Wait — we re-sorted by arrival. After qsort by arrival: P1(0), P3(4), P4(4), P2(5)
      Their orig_idx are: P1=0, P3=2, P4=3, P2=1 (original positions in CSV).
      
      At t=5: P3 (orig_idx=2) and P2 (orig_idx=1) both have burst=4.
      Tie-break: smaller orig_idx wins → P3(2) vs P2(1) → P2 wins? 
      But output shows P3 runs at 5→9, P2 at 9→13.
      
      Re-checking: after qsort the array is [P1, P3, P4, P2] with array indices 0,1,2,3.
      The code uses array index `i` for tie-breaking (i < best), not orig_idx.
      At t=5, scanning: P3 is at index 1, P2 is at index 3 in the sorted array.
      Index 1 < index 3, so P3 wins. → Run P3: 5→9.
t=9:  Run P2: 9→13.

Waiting times:
  P1: start=0, arrival=0  → wait=0
  P3: start=5, arrival=4  → wait=1
  P4: start=4, arrival=4  → wait=0
  P2: start=9, arrival=5  → wait=4

Average = (0+1+0+4)/4 = 5/4 = 1.25 ✓ (matches output1.txt)
```

---

### 4.5 Priority Scheduling

**Rule:** Among arrived processes, run the one with the **lowest priority number** (lower number = higher priority). Ties broken by array position.

**Properties:**
- Non-preemptive (in this assignment)
- Can cause **starvation** of low-priority (high-number) processes
- Real OS uses **aging** to prevent starvation (priority improves with wait time)

**Trace — processes1.csv:**
```
t=0:  Only P1 (priority=5). Run P1: 0→3.
t=3:  Idle 3→4.
t=4:  P3(priority=1) and P4(priority=2). Lowest number = P3(1). Run P3: 4→8.
t=8:  P4(priority=2) and P2(priority=4). P4 wins. Run P4: 8→9.
t=9:  P2(priority=4). Run P2: 9→13.

Waiting times:
  P1: 0, P3: 0, P4: 4, P2: 4
Average = (0+0+4+4)/4 = 2.00 ✓
```

Note: In this example, Priority gives the same result as FCFS because P3 (highest priority, priority=1) also arrived earliest among the t=4 group.

---

### 4.6 Round Robin

**Rule:** Each process gets a fixed time slice (quantum). When quantum expires, process goes to back of queue. If remaining time < quantum, it runs for remaining time only.

**Properties:**
- Preemptive: processes are forcibly interrupted
- Designed for time-sharing systems (every process gets regular CPU time)
- The choice of quantum affects performance:
  - Very small quantum → high context-switch overhead, behaves like ideal time-sharing
  - Very large quantum → behaves like FCFS
- No starvation (every process eventually gets CPU)

**Trace — processes1.csv with quantum=2:**
```
Sorted processes (by arrival): P1(0,burst=3), P3(4,burst=4), P4(4,burst=1), P2(5,burst=4)

t=0:  Queue = [P1]. Pop P1. slice=min(3,2)=2. Print "0→2: P1 Running..."
      After: remaining[P1]=1. New arrivals before t=2? None.
      Push P1 back: queue=[P1].
t=2:  Pop P1. slice=min(1,2)=1. Print "2→3: P1 Running..."
      After: remaining[P1]=0. Done. Kill P1.
      New arrivals at t≤3? P3(4)>3, P4(4)>3 → none.
t=3:  Queue empty! Next arrival = P3 at t=4. Idle 3→4.
      Push P3 and P4 (both arrive at 4): queue=[P3,P4].
t=4:  Pop P3. slice=min(4,2)=2. Print "4→6: P3 Running..."
      After: remaining[P3]=2. New arrivals at t≤6? P2(5)≤6 → push P2.
      Push P3 back: queue=[P4, P2, P3].
t=6:  Pop P4. slice=min(1,2)=1. Print "6→7: P4 Running..."
      After: remaining[P4]=0. Done. Kill P4.
      queue=[P2, P3].
t=7:  Pop P2. slice=min(4,2)=2. Print "7→9: P2 Running..."
      After: remaining[P2]=2. Push P2 back: queue=[P3, P2].
t=9:  Pop P3. slice=min(2,2)=2. Print "9→11: P3 Running..."
      After: remaining[P3]=0. Done. Kill P3. queue=[P2].
t=11: Pop P2. slice=min(2,2)=2. Print "11→13: P2 Running..."
      After: remaining[P2]=0. Done. Kill P2. queue=[].

Total Turnaround Time = t = 13 ✓ (matches output1.txt)
```

---

### 4.7 Process Creation with `fork()`

`fork()` creates an exact copy of the calling process.

```c
pid_t pid = fork();
if (pid < 0) {
    perror("fork");   // error
    exit(1);
}
if (pid == 0) {
    // Child process — pid returned is 0
    // Do child work here
    _exit(0);
}
// Parent process — pid is the child's PID
waitpid(pid, NULL, 0);   // wait for child to finish
```

**Return values:**
- `fork()` returns `0` in the **child**
- `fork()` returns the child's **PID** in the **parent**
- `fork()` returns `-1` on failure

**After `fork()`:**
- Child gets a copy of parent's entire address space (virtual memory, file descriptors, signal handlers, etc.)
- They are separate processes — changes in one do not affect the other
- Both processes resume execution at the line after `fork()`

**`_exit(0)` vs `exit(0)`:**
- `_exit()` terminates immediately without flushing stdio buffers — used in forked children to avoid double-flushing the parent's buffers
- `exit()` flushes buffers, calls atexit handlers — correct for the main program

**`waitpid(pid, &status, options)`:**
- Waits for process `pid` to change state
- `status` can be inspected with macros like `WIFEXITED()`, `WEXITSTATUS()`
- Prevents **zombie processes** (finished processes whose exit status hasn't been collected)

---

### 4.8 Killing and Controlling Processes: `kill()`, SIGSTOP, SIGCONT, SIGKILL

```c
kill(pid, signal);   // send 'signal' to process 'pid'
```

**In the Round Robin scheduler:**
- When a process's quantum expires but it has remaining burst: `kill(pid, SIGSTOP)` → process is paused
- When a process's turn comes again: `kill(pid, SIGCONT)` → process resumes
- When a process's remaining time reaches 0: `kill(pid, SIGKILL)` → process is terminated

**SIGSTOP vs SIGKILL:**
- `SIGSTOP` and `SIGKILL` **cannot be caught, blocked, or ignored** — the kernel always handles them directly
- `SIGSTOP` pauses the process; `SIGCONT` resumes it
- `SIGKILL` immediately terminates the process

**The child process in Round Robin:**
```c
// From CPU-Scheduler.c — Round Robin child
if (cpids[idx] == 0) {    // child
    while (1) pause();    // wait forever for signals
    _exit(0);             // never reached
}
```
The child just loops on `pause()`, which suspends it until any signal arrives. The parent uses SIGSTOP/SIGCONT to pause/resume it, and SIGKILL when done.

**For non-preemptive algorithms (FCFS, SJF, Priority):**
```c
// From CPU-Scheduler.c — non-preemptive child
if (pid == 0) {       // child
    alarm(burst);     // set timer for burst seconds
    pause();          // sleep until SIGALRM fires
    _exit(0);
}
waitpid(pid, NULL, 0);   // parent waits
```
Child sets its own alarm, pauses, and exits when the alarm fires.

---

### 4.9 Time Simulation with `alarm()` and `sigsuspend()`

**`alarm(seconds)`:**
- Schedules a `SIGALRM` to be sent to the process after `seconds` seconds
- Returns the remaining alarm time from any previous `alarm()` call
- `alarm(0)` cancels any pending alarm

**The naive approach (WRONG — has a race condition):**
```c
g_alrm = 0;
alarm(sec);
while (!g_alrm)
    pause();     // RACE CONDITION HERE!
```

Why is this wrong? Consider this sequence:
```
1. g_alrm = 0
2. alarm(sec) is scheduled
3. SIGALRM fires! → g_alrm = 1
4. while(!g_alrm) → false, skip loop
   [no problem this time... but:]

Alternative sequence:
1. g_alrm = 0
2. alarm(sec) scheduled
3. alarm fires BETWEEN the check `!g_alrm` and `pause()`
4. `pause()` now waits forever — the signal already arrived!
```

**The correct approach (what the code actually uses):**
```c
// From CPU-Scheduler.c — wait_seconds()

static void wait_seconds(int sec) {
    if (sec <= 0) return;

    sigset_t block_alrm, old_mask, wait_mask;
    sigemptyset(&block_alrm);
    sigaddset(&block_alrm, SIGALRM);

    // 1. Block SIGALRM so it cannot arrive while we set up
    sigprocmask(SIG_BLOCK, &block_alrm, &old_mask);

    g_alrm = 0;
    alarm(sec);

    // 2. Build the mask for sigsuspend: everything except SIGALRM
    wait_mask = old_mask;
    sigdelset(&wait_mask, SIGALRM);

    // 3. sigsuspend atomically: restore wait_mask AND sleep
    //    It is IMPOSSIBLE for SIGALRM to arrive between the
    //    "unblock" and the "sleep" because it's a single atomic operation.
    while (!g_alrm)
        sigsuspend(&wait_mask);

    // 4. Restore original signal mask
    sigprocmask(SIG_SETMASK, &old_mask, NULL);
}
```

**`sigsuspend(&mask)`:**
- Atomically replaces the signal mask with `mask` and suspends the process until a signal arrives
- When a signal arrives, the original mask is restored and `sigsuspend` returns
- This is the **standard solution** to the alarm race condition

**`SA_RESTART` flag** (used in CPU-Scheduler for SIGALRM handler):
```c
struct sigaction sa;
sa.sa_flags = SA_RESTART;
sa.sa_handler = alrm_handler;
sigaction(SIGALRM, &sa, NULL);
```
This makes system calls that are interrupted by SIGALRM automatically restart (instead of returning `EINTR`).

**`volatile sig_atomic_t`:**
```c
static volatile sig_atomic_t g_alrm = 0;
```
- `volatile` — tells the compiler this variable can change unexpectedly (don't cache in register)
- `sig_atomic_t` — an integer type that can be read/written atomically (safe from signal handlers)
- Without these qualifiers, the compiler might optimize away the `while (!g_alrm)` check

---

### 4.10 Race Conditions and How They Are Avoided

**Race condition:** When the correctness of a program depends on the exact timing of two or more concurrent events.

| Race Condition | Problem | Solution Used |
|----------------|---------|---------------|
| alarm fires before `pause()` | `pause()` waits forever | `sigsuspend()` — atomic unblock+sleep |
| SIGALRM mask at wrong time | Child alarm interferes with parent | Block SIGALRM before `alarm()`, use `sigsuspend()` |
| Parent reads child output | None (no shared memory) | Fork-based isolation |

---

### 4.11 CPU-Scheduler.c — Full Annotated Walkthrough

**File path:** `CPU-Scheduler.c`

#### Data Structures

```c
// Process record read from CSV
typedef struct {
    char name[MAX_NAME];    // e.g., "P1"
    char desc[MAX_DESC];    // e.g., "System Bootloader"
    int  arrival;           // arrival time in seconds
    int  burst;             // total CPU time needed
    int  priority;          // lower number = higher priority
    int  orig_idx;          // original position in CSV (for tie-breaking)
} SchedProc;

// Simple circular integer queue — used for Round Robin ready queue
typedef struct {
    int data[MAX_PROCS];
    int front, back, size;
} IntQueue;
```

#### CSV Parsing

```c
// From CPU-Scheduler.c — parse_csv()

static int parse_csv(const char *path, SchedProc procs[]) {
    FILE *fp = fopen(path, "r");
    if (!fp) { perror("fopen"); exit(1); }

    char line[MAX_LINE];
    int n = 0;

    while (fgets(line, sizeof(line), fp) && n < MAX_PROCS) {
        line[strcspn(line, "\r\n")] = '\0';  // strip newline
        if (line[0] == '\0' || line[0] == '#') continue;  // skip blank/comment

        SchedProc *p = &procs[n];

        tok = strtok(line, ",");   // tokenize by comma
        strncpy(p->name, tok, MAX_NAME - 1);

        tok = strtok(NULL, ",");   // subsequent calls with NULL continue tokenizing
        strncpy(p->desc, tok, MAX_DESC - 1);

        tok = strtok(NULL, ",");
        p->arrival = atoi(tok);

        tok = strtok(NULL, ",");
        p->burst = atoi(tok);

        tok = strtok(NULL, ",");
        p->priority = atoi(tok);

        p->orig_idx = n;  // record original position BEFORE sorting
        n++;
    }
    fclose(fp);
    return n;
}
```

**Note:** The CSV files start with comment lines (`# gcc ex3.c -o ex3`) that the parser skips with the `line[0] == '#'` check.

#### Sorting (Stable by Arrival)

```c
// Comparator: sort by arrival time; tie-break by orig_idx (original CSV order)
static int cmp_arrival(const void *a, const void *b) {
    const SchedProc *pa = (const SchedProc *)a;
    const SchedProc *pb = (const SchedProc *)b;
    if (pa->arrival != pb->arrival)
        return pa->arrival - pb->arrival;   // ascending by arrival
    return pa->orig_idx - pb->orig_idx;     // ascending by original position (stable)
}

// Called once before any scheduling:
qsort(procs, n, sizeof(SchedProc), cmp_arrival);
```

`qsort` is the C standard library sort. Its time complexity is O(n log n). The comparator returning negative means "a before b", positive means "b before a", 0 means "equal".

#### Non-Preemptive Scheduler (FCFS, SJF, Priority)

```c
// From CPU-Scheduler.c — run_nonpreemptive()

static void run_nonpreemptive(SchedProc procs[], int n, int policy, const char *label) {
    sched_header(label);

    int *done = calloc(n, sizeof(int));   // marks completed processes
    int t = 0, finished = 0;
    double wait_sum = 0.0;

    while (finished < n) {

        // Find the best process to run at time t
        int best = -1;
        for (int i = 0; i < n; i++) {
            if (done[i] || procs[i].arrival > t) continue;  // skip done or not-yet-arrived
            if (best < 0) { best = i; continue; }

            int swap = 0;
            if (policy == POL_FCFS) {
                // FCFS: prefer earlier arrival
                if (procs[i].arrival < procs[best].arrival) swap = 1;
                else if (procs[i].arrival == procs[best].arrival && i < best) swap = 1;
            } else if (policy == POL_SJF) {
                // SJF: prefer shorter burst
                if (procs[i].burst < procs[best].burst) swap = 1;
                else if (procs[i].burst == procs[best].burst && i < best) swap = 1;
            } else {   // POL_PRIO
                // Priority: prefer lower priority number
                if (procs[i].priority < procs[best].priority) swap = 1;
                else if (procs[i].priority == procs[best].priority && i < best) swap = 1;
            }
            if (swap) best = i;
        }

        // If no process is ready: idle until next arrival
        if (best < 0) {
            int earliest = -1;
            for (int i = 0; i < n; i++) {
                if (!done[i] && procs[i].arrival > t)
                    if (earliest < 0 || procs[i].arrival < procs[earliest].arrival)
                        earliest = i;
            }
            printf("%d → %d: Idle.\n", t, procs[earliest].arrival);
            fflush(stdout);
            wait_seconds(procs[earliest].arrival - t);
            t = procs[earliest].arrival;
            continue;
        }

        // Run the selected process
        int end = t + procs[best].burst;
        wait_sum += (double)(t - procs[best].arrival);   // accumulate waiting time

        printf("%d → %d: %s Running %s.\n", t, end, procs[best].name, procs[best].desc);
        fflush(stdout);

        sim_fork_and_wait(procs[best].burst);   // fork child, simulate burst, wait

        t = end;
        done[best] = 1;
        finished++;
    }

    sched_footer_avg(wait_sum / n);
    free(done);
}
```

**`sim_fork_and_wait`:** This is a helper that creates a child process to simulate execution time:
```c
static void sim_fork_and_wait(int burst) {
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); exit(1); }

    if (pid == 0) {
        alarm(burst);   // child sets its own alarm
        pause();        // child waits for SIGALRM
        _exit(0);       // child exits
    }

    waitpid(pid, NULL, 0);   // parent waits for child to finish
}
```
The child's SIGALRM handler was inherited from the parent (the `alrm_handler` set in `runCPUScheduler`). But here the child actually just needs to die — `pause()` waits for SIGALRM, which by default would terminate the process... but the parent set `SA_RESTART` and installed `alrm_handler` which just sets `g_alrm=1`. After the handler runs, `pause()` returns (since a signal interrupted it). Then `_exit(0)` is called.

Actually — the child inherits the handler. So `pause()` returns after SIGALRM fires. Then `_exit(0)`.

#### Round Robin Scheduler

```c
// From CPU-Scheduler.c — run_round_robin()

static void run_round_robin(SchedProc procs[], int n, int quantum) {
    sched_header("Round Robin");

    int remaining[MAX_PROCS];    // remaining burst for each process
    pid_t cpids[MAX_PROCS];      // child PIDs (one long-lived child per process)
    int forked[MAX_PROCS];       // has this process been forked yet?

    for (int i = 0; i < n; i++) {
        remaining[i] = procs[i].burst;
        cpids[i] = -1;
        forked[i] = 0;
    }

    IntQueue rq;
    iq_init(&rq);

    int t = 0, next_p = 0, done = 0;

    // Add processes that arrive at t=0
    while (next_p < n && procs[next_p].arrival <= t) {
        iq_push(&rq, next_p);
        next_p++;
    }

    while (done < n) {

        if (iq_empty(&rq)) {
            // CPU is idle; jump to next arrival
            int next_arr = procs[next_p].arrival;
            printf("%d → %d: Idle.\n", t, next_arr);
            fflush(stdout);
            wait_seconds(next_arr - t);
            t = next_arr;
            while (next_p < n && procs[next_p].arrival <= t) {
                iq_push(&rq, next_p);
                next_p++;
            }
            continue;
        }

        int idx = iq_pop(&rq);
        int slice = (remaining[idx] < quantum) ? remaining[idx] : quantum;

        printf("%d → %d: %s Running %s.\n",
               t, t + slice, procs[idx].name, procs[idx].desc);
        fflush(stdout);

        // Create child on first run, resume on subsequent runs
        if (!forked[idx]) {
            cpids[idx] = fork();
            if (cpids[idx] == 0) {
                while (1) pause();   // child waits forever
                _exit(0);
            }
            forked[idx] = 1;
        } else {
            kill(cpids[idx], SIGCONT);   // resume paused child
        }

        wait_seconds(slice);   // parent simulates the time slice

        t += slice;
        remaining[idx] -= slice;

        // Enqueue newly arrived processes
        while (next_p < n && procs[next_p].arrival <= t) {
            iq_push(&rq, next_p);
            next_p++;
        }

        if (remaining[idx] > 0) {
            kill(cpids[idx], SIGSTOP);   // pause child
            iq_push(&rq, idx);           // re-queue for next turn
        } else {
            kill(cpids[idx], SIGKILL);   // terminate child
            waitpid(cpids[idx], NULL, 0);
            done++;
        }
    }

    sched_footer_rr(t);   // t is the total turnaround time
}
```

**Why long-lived child processes in Round Robin?**
- Real OS schedulers context-switch the same process multiple times
- Having one persistent child per process (SIGSTOP/SIGCONT) more faithfully models a real scheduler
- For non-preemptive algorithms, a new child per process is fine since each runs once to completion

---

## 5. Worked Examples — Trace Every Test Case

### Test Case 1 (processes1.csv, quantum=2) — Complete Trace

**Input:**
```
P1,System Bootloader,0,3,5
P2,Kernel Initialization,5,4,4
P3,Device Driver Loading,4,4,1
P4,Network Services Startup,4,1,2
```

**After sorting by arrival:** P1(arr=0), P3(arr=4), P4(arr=4), P2(arr=5)
(P3 before P4 because P3 is at orig_idx=2, P4 at orig_idx=3 — wait, in the CSV P1=0, P2=1, P3=2, P4=3, but after sorting by arrival they become array indices 0,1,2,3 as [P1, P3, P4, P2])

**FCFS result:** 0→3: P1 | 3→4: Idle | 4→8: P3 | 8→9: P4 | 9→13: P2 | Avg wait = 2.00
**SJF result:** 0→3: P1 | 3→4: Idle | 4→5: P4 | 5→9: P3 | 9→13: P2 | Avg wait = 1.25
**Priority result:** 0→3: P1 | 3→4: Idle | 4→8: P3 | 8→9: P4 | 9→13: P2 | Avg wait = 2.00
**Round Robin (q=2):** 0→2: P1 | 2→3: P1 | 3→4: Idle | 4→6: P3 | 6→7: P4 | 7→9: P2 | 9→11: P3 | 11→13: P2 | Total turnaround = 13

---

### Test Case 2 (processes2.csv, quantum=20) — All Arrive at t=0

**Input:**
```
P1,User Login Service,0,10,3
P2,Graphical Session Initialization,0,1,1
P3,Power Management Service,0,2,4
P4,System Update Daemon,0,1,5
P5,Filesystem Mount Manager,0,5,2
```

All arrive at t=0. With quantum=20 (larger than any burst), Round Robin degenerates to FCFS-like behavior (each process completes in one turn).

**FCFS** (arrival order = CSV order since all arrive at 0):
- 0→10: P1 | 10→11: P2 | 11→13: P3 | 13→14: P4 | 14→19: P5
- Waits: P1=0, P2=10, P3=11, P4=13, P5=14 → avg = 48/5 = 9.60

**SJF** (sort by burst: P2=1, P4=1, P5=5, P3=2, P1=10 → actually: P2=1, P4=1, P3=2, P5=5, P1=10):
- Tie between P2 and P4 (both burst=1): P2 has smaller array index → P2 first
- 0→1: P2 | 1→2: P4 | 2→4: P3 | 4→9: P5 | 9→19: P1
- Waits: P2=0, P4=1, P3=2, P5=4, P1=9 → avg = 16/5 = 3.20

**Priority** (lower number = higher priority: P2=1, P5=2, P1=3, P3=4, P4=5):
- 0→1: P2 | 1→6: P5 | 6→16: P1 | 16→18: P3 | 18→19: P4
- Waits: P2=0, P5=1, P1=6, P3=16, P4=18 → avg = 41/5 = 8.20

**Round Robin (q=20):** quantum=20 > all bursts, so each process runs once fully.
- Order: CSV order (FCFS) since all arrive at 0
- Total time = 10+1+2+1+5 = 19 → Turnaround = 19

---

### Test Case 3 (processes3.csv, quantum=3) — Staggered, No Overlap at Arrival

**Input:**
```
P3,System Bootloader,3,3,3
P2,Kernel Initialization,2,2,2
P1,Device Driver Loading,1,1,1
P4,Network Services Startup,10,10,10
```

After sorting by arrival: P1(arr=1,burst=1), P2(arr=2,burst=2), P3(arr=3,burst=3), P4(arr=10,burst=10)

**FCFS:** Each process arrives just as the previous one completes (or earlier). Wait times are all 0.
- 0→1: Idle | 1→2: P1 | 2→4: P2 | 4→7: P3 | 7→10: Idle | 10→20: P4
- Wait: P1=0, P2=0, P3=1? → P3 arrives at 3, starts at 4 → wait=1? No wait... P2 completes at t=4, P3 arrives at t=3. So P3 is in ready queue waiting from t=3 to t=4 → wait=1.

Actually let me recheck: P1 arrives t=1, starts t=1 → wait=0. P2 arrives t=2, starts t=2 → wait=0. P3 arrives t=3, starts t=4 (P2 finishes at 4) → wait=1. P4 arrives t=10, starts t=10 → wait=0.
Avg = (0+0+1+0)/4 = 0.25

All three non-preemptive give same result here: avg = 0.25

**Round Robin (q=3):** P1(burst=1)<q so runs fully. P2(burst=2)<q. P3(burst=3)=q. P4(burst=10)→4 slices.
After sorting: P1, P2, P3, P4 (arrivals 1, 2, 3, 10)
- t=0: Queue empty. Idle 0→1. Push P1.
- t=1: Pop P1, slice=1. 1→2: P1. Done. Push P2 (arr=2≤2). Queue=[P2].
- t=2: Pop P2, slice=2. 2→4: P2. Done. Push P3 (arr=3≤4). Queue=[P3].
- t=4: Pop P3, slice=3. 4→7: P3. Done. Queue=[].
- t=7: Idle 7→10. Push P4 (arr=10).
- t=10: Pop P4, slice=3. 10→13: P4. remaining=7. Push P4.
- t=13: Pop P4, slice=3. 13→16: P4. remaining=4. Push P4.
- t=16: Pop P4, slice=3. 16→19: P4. remaining=1. Push P4.
- t=19: Pop P4, slice=1. 19→20: P4. Done.
- Total turnaround = 20 ✓

---

## 6. Key System Calls — Quick Reference

| System Call | Signature | Purpose | Used In |
|-------------|-----------|---------|---------|
| `sigaction` | `int sigaction(int sig, const struct sigaction *act, struct sigaction *old)` | Register signal handler | Both parts |
| `sigprocmask` | `int sigprocmask(int how, const sigset_t *set, sigset_t *old)` | Block/unblock signals | Both parts |
| `sigpending` | `int sigpending(sigset_t *set)` | Get set of pending signals | Focus Mode |
| `sigismember` | `int sigismember(const sigset_t *set, int sig)` | Test if signal is in set | Focus Mode |
| `sigemptyset` | `int sigemptyset(sigset_t *set)` | Initialize empty signal set | Both parts |
| `sigaddset` | `int sigaddset(sigset_t *set, int sig)` | Add signal to set | Both parts |
| `sigdelset` | `int sigdelset(sigset_t *set, int sig)` | Remove signal from set | CPU Scheduler |
| `sigsuspend` | `int sigsuspend(const sigset_t *mask)` | Atomic: set mask and sleep | CPU Scheduler |
| `raise` | `int raise(int sig)` | Send signal to self | Focus Mode |
| `kill` | `int kill(pid_t pid, int sig)` | Send signal to process | CPU Scheduler |
| `alarm` | `unsigned alarm(unsigned seconds)` | Schedule SIGALRM | CPU Scheduler |
| `pause` | `int pause(void)` | Sleep until signal | CPU Scheduler |
| `fork` | `pid_t fork(void)` | Create child process | CPU Scheduler |
| `waitpid` | `pid_t waitpid(pid_t pid, int *stat, int opts)` | Wait for process state change | CPU Scheduler |
| `write` | `ssize_t write(int fd, const void *buf, size_t count)` | Async-signal-safe output | Focus Mode handlers |

**Grading requirement (Focus Mode):** Must use ALL of: `sigaction`, `sigprocmask`, `sigpending`, `sigismember`. Missing any = **0 points**.

**Grading requirement (CPU Scheduler):** Must use at least one of: `signal`, `sigaction`, `sigprocmask`, `sigpending`, `sigismember`. Missing all = **0 points**.

---

## 7. Common Exam Questions & Answers

**Q: What is the difference between a signal being blocked vs. ignored?**
A: **Blocked**: signal is held pending and delivered later when unblocked. **Ignored**: signal is discarded immediately and never delivered.

**Q: Can you block SIGKILL or SIGSTOP?**
A: No. These two signals cannot be caught, blocked, or ignored. The kernel always handles them directly. This is by design — it guarantees the administrator can always kill a process.

**Q: Why does the assignment use `sigaction` instead of `signal()`?**
A: `signal()` behavior is implementation-defined. On some systems it auto-resets to `SIG_DFL` after one delivery. `sigaction()` is guaranteed portable, allows blocking additional signals during handler execution, and supports `SA_RESTART`.

**Q: What happens if you call `raise(SIGUSR1)` twice while SIGUSR1 is blocked?**
A: Only one instance becomes pending. The second raise is silently dropped. The kernel uses a bitmask (not a queue) for pending signals, so the same signal cannot appear twice in the pending set.

**Q: What is the race condition in `alarm` + `pause()` and how does `sigsuspend` fix it?**
A: If SIGALRM fires between the `alarm()` call and the `pause()` call, `pause()` will wait forever. `sigsuspend(&mask)` atomically unblocks the signal and suspends the process — there is no window where the signal can arrive and be missed.

**Q: What is the waiting time for a process in this scheduler?**
A: Waiting Time = Start Time − Arrival Time. Idle periods (no process ready) do NOT count as waiting time for any process.

**Q: What is the turnaround time definition in Round Robin here?**
A: Total elapsed time from t=0 until the last process completes. This equals the final clock value `t` at the end of Round Robin.

**Q: In Round Robin, when a process is preempted (quantum expires but not done), what happens?**
A: The scheduler sends it SIGSTOP (pausing it), enqueues it back at the end of the ready queue, and picks the next process in the queue.

**Q: Why does the Round Robin use long-lived child processes while FCFS/SJF/Priority fork a new child each time?**
A: Round Robin needs to pause and resume the same process multiple times (SIGSTOP/SIGCONT), requiring a persistent child. Non-preemptive algorithms run each process to completion once, so a fresh fork is fine.

**Q: What is the convoy effect in FCFS?**
A: If a long-burst process arrives first, all shorter processes arriving after it must wait in the ready queue while the long job runs. This is like a convoy of cars stuck behind a slow truck. SJF eliminates this by always running the shortest available job.

**Q: Why can SJF cause starvation?**
A: If short jobs keep arriving, they always preempt longer jobs in the scheduling decision. A long job may never get scheduled. Round Robin prevents starvation because every process in the queue gets a turn in bounded time.

**Q: What is `async-signal-safe` and why does it matter?**
A: A function is async-signal-safe if it can safely be called from a signal handler while the same function may be running in the main program. `printf()` uses internal locks and buffers — if a signal interrupts `printf()` and the handler also calls `printf()`, the lock causes deadlock. `write()` is a direct kernel call with no user-space buffering, so it is safe.

**Q: What does `_exit()` do differently from `exit()`?**
A: `_exit()` terminates immediately without flushing stdio buffers or calling `atexit()` handlers. This is important in forked children — if they called `exit()`, they could flush the parent's unflushed output buffers, causing double output.

**Q: What is a zombie process?**
A: A process that has finished executing but whose exit status has not yet been collected by its parent via `wait()` or `waitpid()`. The process entry remains in the process table until the parent collects it.

**Q: Explain the tie-breaking rule used throughout the scheduler.**
A: When two processes are equal under the current algorithm's criterion (same arrival time for FCFS, same burst for SJF, same priority for Priority), the one with the smaller array index in the sorted array wins. Since the array is sorted stably by arrival time, this preserves the original CSV order for equal-priority ties.

**Q: What is the difference between preemptive and non-preemptive scheduling?**
A: In **non-preemptive** scheduling, once a process starts running it runs until completion. In **preemptive** scheduling, the OS can forcibly remove the process from the CPU (e.g., when its quantum expires or a higher-priority process arrives). FCFS, SJF, and Priority here are non-preemptive. Round Robin is preemptive.

**Q: What does `SA_RESTART` do in the SIGALRM handler?**
A: Normally, when a signal interrupts a slow system call (like `read()` or `pause()`), the call returns with `errno = EINTR`. With `SA_RESTART`, interrupted calls are automatically restarted by the kernel, so the code does not need to handle `EINTR` manually.

---

## 8. Bug in ex3.c — Know This

**File path:** `ex3.c` — Line 20

```c
// BUGGY: uses && (AND), so condition is always FALSE
// A string cannot equal BOTH "Focus-Mode" AND "CPU-Scheduler" at the same time
if (!strcmp(argv[1], "Focus-Mode") && !strcmp(argv[1], "CPU-Scheduler")) {
```

This branch is **dead code** — it never triggers. The intent was to reject invalid first arguments, but because of `&&` instead of `||`, no invalid input ever hits this check.

**What it should be** (to reject invalid mode names):
```c
// CORRECT: if it's neither Focus-Mode NOR CPU-Scheduler, reject it
if (strcmp(argv[1], "Focus-Mode") != 0 && strcmp(argv[1], "CPU-Scheduler") != 0) {
    printf("Usage: ...");
    exit(0);
}
```

This is a common logical error: confusing De Morgan's law.
- "Neither A nor B" = `!A && !B` = `!(A || B)`
- The bug writes `!A && !B` as `(!strcmp(A,...)) && (!strcmp(B,...))` which expands to `(A==X) && (A==Y)` — which can never be true for two different strings X and Y.

**For the exam:** You may be asked to identify or fix this. The program still works correctly for valid inputs (`Focus-Mode` and `CPU-Scheduler`) — only invalid modes like `./ex3 Banana 3 2` would not be caught.

---

## Summary: The Two Core Mental Models

### Focus Mode = Signal Mask as a "Do Not Disturb" Mode

```
BLOCK → [ work time ] → CHECK PENDING → DELIVER EACH → UNBLOCK for next round
```
The signal mask is your "DND" switch. Signals pile up (pending) while you're focused, and you handle them deliberately at the end of each round — in a fixed, controlled order.

### CPU Scheduler = Parent as OS Kernel, Children as User Processes

```
PARENT (scheduler) ─── fork() ──→ CHILD (process)
                   ─── kill(SIGSTOP) ──→ pause/resume
                   ─── kill(SIGCONT) ──→
                   ─── kill(SIGKILL) ──→ terminate
                   ─── alarm()+sigsuspend() ──→ measure time
```
The parent simulates the OS kernel's scheduling logic. Each forked child represents a user process. `alarm()` + `sigsuspend()` gives the parent an atomic, race-free way to wait exactly N seconds. SIGSTOP/SIGCONT/SIGKILL give the parent complete control over each child.

---

*End of Tutor Guide. Good luck on the exam!*
