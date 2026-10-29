# Signals & CPU Scheduling (OS Assignment 3)

A C program with two modes:

- **Focus Mode** (`Focus-Mode.c`) — simulates a student staying focused while distractions arrive as Unix signals; practices `sigaction`, `sigprocmask`, `sigpending`, and `raise`.
- **CPU Scheduler** (`CPU-Scheduler.c`) — reads processes from a CSV and simulates **FCFS**, **SJF**, **Priority** (non-preemptive), and **Round Robin**, reporting per-algorithm statistics.

`ex3.c` validates arguments and dispatches to the selected mode.

## Build & run
```bash
gcc ex3.c -o ex3
./ex3 Focus-Mode 5 3                                     # <rounds> <round-duration>
./ex3 CPU-Scheduler CPU-Scheduler-Tests/processes1.csv 2 # <csv> <RR time quantum>
```
Sample inputs/expected outputs are in `Focus-Mode-Tests/` and `CPU-Scheduler-Tests/`.

See [ASSIGNMENT.md](ASSIGNMENT.md) for the full spec and [TUTOR.md](TUTOR.md) for study notes.
