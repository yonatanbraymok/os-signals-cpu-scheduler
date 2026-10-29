/*
 * ============================================================================
 * CPU-Scheduler.c — CPU Scheduling Simulator (Main Part, 80% of the grade)
 * ============================================================================
 *
 * GOAL:
 *   Build a lightweight "OS kernel" that reads processes from a CSV file and
 *   simulates how a real operating system schedules them on the CPU.
 *
 * WHAT THIS MODULE DOES:
 *   1. Parse a CSV of processes (name, description, arrival, burst, priority).
 *   2. Sort them by arrival time (stable — ties keep original CSV order).
 *   3. Run ALL FOUR scheduling algorithms sequentially on the same data:
 *        FCFS → SJF → Priority → Round Robin
 *   4. Print a timeline of what runs when, plus summary statistics.
 *
 * HOW TIME IS SIMULATED (no busy-wait loops):
 *   - alarm() + sigsuspend() — parent waits for N seconds (idle or time slice)
 *   - fork() + pause()       — child "runs" for its burst time then exits
 *   - kill(SIGSTOP/SIGCONT)  — Round Robin preempts and resumes child processes
 *
 * PROCESS MODEL:
 *   Parent = scheduler (decides who runs, tracks clock `t`)
 *   Child  = process     (simulates CPU work by sleeping)
 *   NO threads — only fork() as required by the assignment.
 *
 * SCHEDULING POLICIES:
 *   FCFS     — First Come First Served (non-preemptive, by arrival time)
 *   SJF      — Shortest Job First     (non-preemptive, shortest burst wins)
 *   Priority — Lowest priority number runs next (non-preemptive)
 *   RR       — Round Robin            (preemptive, fixed time quantum)
 *
 * METRICS:
 *   FCFS/SJF/Priority → Average Waiting Time = sum(start - arrival) / n
 *   Round Robin       → Total Turnaround Time = clock when last process finishes
 * ============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>

#define MAX_PROCS   1000   /* assignment guarantees ≤ 1000 processes */
#define MAX_LINE    256    /* max characters per CSV line */
#define MAX_NAME    51     /* name field ≤ 50 chars + null terminator */
#define MAX_DESC    101    /* description field ≤ 100 chars + null terminator */

/*
 * SchedProc — simplified Process Control Block (PCB).
 * Holds everything we need to schedule one process.
 * orig_idx preserves CSV row order for stable tie-breaking.
 */
typedef struct {
    char name[MAX_NAME];
    char desc[MAX_DESC];
    int  arrival;    /* time (seconds) when process becomes ready */
    int  burst;      /* total CPU time the process needs */
    int  priority;   /* lower number = higher priority */
    int  orig_idx;   /* original row index in CSV (for stable sort / ties) */
} SchedProc;

/*
 * IntQueue — circular buffer used as the Round Robin ready queue.
 * Stores process indices; front = next to run, back = where to push.
 */
typedef struct {
    int data[MAX_PROCS];
    int front, back, size;
} IntQueue;

static void iq_init(IntQueue *q) { q->front = q->back = q->size = 0; }

static void iq_push(IntQueue *q, int v) {
    q->data[q->back] = v;
    q->back = (q->back + 1) % MAX_PROCS;
    q->size++;
}

static int iq_pop(IntQueue *q) {
    int v = q->data[q->front];
    q->front = (q->front + 1) % MAX_PROCS;
    q->size--;
    return v;
}

static int iq_empty(IntQueue *q) { return q->size == 0; }

/*
 * g_alrm — flag set by the SIGALRM handler when alarm() fires.
 * volatile sig_atomic_t ensures the compiler won't optimize away reads
 * and that updates from the handler are visible to the main code.
 */
static volatile sig_atomic_t g_alrm = 0;

/* SIGALRM handler — just flip the flag; actual waiting happens in wait_seconds() */
static void alrm_handler(int sig) {
    (void)sig;
    g_alrm = 1;
}

/* ============================================================================
 * CSV PARSING
 * ============================================================================
 * Reads processes from a file with format:
 *   Name,Description,Arrival Time,Burst Time,Priority
 * No header row. Lines starting with '#' or empty lines are skipped.
 */

static int parse_csv(const char *path, SchedProc procs[]) {
    FILE *fp = fopen(path, "r");
    if (!fp) { perror("fopen"); exit(1); }

    char line[MAX_LINE];
    int n = 0;

    while (fgets(line, sizeof(line), fp) && n < MAX_PROCS) {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '\0' || line[0] == '#') continue;

        SchedProc *p = &procs[n];
        char *tok;

        tok = strtok(line, ",");  if (!tok) continue;
        strncpy(p->name, tok, MAX_NAME - 1);  p->name[MAX_NAME - 1] = '\0';

        tok = strtok(NULL, ","); if (!tok) continue;
        strncpy(p->desc, tok, MAX_DESC - 1);  p->desc[MAX_DESC - 1] = '\0';

        tok = strtok(NULL, ","); if (!tok) continue;
        p->arrival = atoi(tok);

        tok = strtok(NULL, ","); if (!tok) continue;
        p->burst = atoi(tok);

        tok = strtok(NULL, ","); if (!tok) continue;
        p->priority = atoi(tok);

        p->orig_idx = n;  /* remember CSV order before sorting */
        n++;
    }

    fclose(fp);
    return n;
}

/* ============================================================================
 * SORTING
 * ============================================================================
 * Stable sort by arrival time. If two processes arrive at the same time,
 * preserve their original CSV order (orig_idx) as required by the assignment.
 */

static int cmp_arrival(const void *a, const void *b) {
    const SchedProc *pa = (const SchedProc *)a;
    const SchedProc *pb = (const SchedProc *)b;
    if (pa->arrival != pb->arrival)
        return pa->arrival - pb->arrival;
    return pa->orig_idx - pb->orig_idx;
}

/* ============================================================================
 * OUTPUT FORMATTING
 * ============================================================================
 * Helpers to print the assignment's required report headers and footers.
 */

static void sched_header(const char *mode) {
    printf("══════════════════════════════════════════════\n");
    printf(">> Scheduler Mode : %s\n", mode);
    printf(">> Engine Status  : Initialized\n");
    printf("──────────────────────────────────────────────\n");
    printf("\n");
}

static void sched_footer_avg(double avg_wait) {
    printf("\n");
    printf("──────────────────────────────────────────────\n");
    printf(">> Engine Status  : Completed\n");
    printf(">> Summary        :\n");
    printf("   └─ Average Waiting Time : %.2f time units\n", avg_wait);
    printf(">> End of Report\n");
    printf("══════════════════════════════════════════════\n");
    printf("\n");
}

static void sched_footer_rr(int turnaround) {
    printf("\n");
    printf("──────────────────────────────────────────────\n");
    printf(">> Engine Status  : Completed\n");
    printf(">> Summary        :\n");
    printf("   └─ Total Turnaround Time : %d time units\n", turnaround);
    printf("\n");
    printf(">> End of Report\n");
    printf("══════════════════════════════════════════════\n");
    printf("\n");
}

/* ============================================================================
 * TIME SIMULATION HELPERS
 * ============================================================================ */

/*
 * wait_seconds — sleep for `sec` seconds using alarm() + sigsuspend().
 *
 * PURPOSE: Simulate the passage of time (idle periods or RR time slices).
 *
 * WHY sigsuspend instead of pause()?
 *   There's a race: alarm() might fire before pause() is called, and the
 *   signal would be lost. We block SIGALRM first, set alarm(), then
 *   sigsuspend() atomically unblocks SIGALRM and sleeps until g_alrm is set.
 */
static void wait_seconds(int sec) {
    if (sec <= 0) return;

    sigset_t block_alrm, old_mask, wait_mask;
    sigemptyset(&block_alrm);
    sigaddset(&block_alrm, SIGALRM);

    sigprocmask(SIG_BLOCK, &block_alrm, &old_mask);

    g_alrm = 0;
    alarm(sec);

    wait_mask = old_mask;
    sigdelset(&wait_mask, SIGALRM);

    while (!g_alrm)
        sigsuspend(&wait_mask);

    sigprocmask(SIG_SETMASK, &old_mask, NULL);
}

/*
 * sim_fork_and_wait — run one process to completion (non-preemptive algorithms).
 *
 * PURPOSE: Fork a child that sleeps for `burst` seconds (simulating CPU work),
 * then the parent waits for it with waitpid(). One fork per process execution.
 */
static void sim_fork_and_wait(int burst) {
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); exit(1); }

    if (pid == 0) {
        /* Child: alarm wakes us after burst seconds; pause() blocks until then */
        alarm(burst);
        pause();
        _exit(0);
    }

    /* Parent: block until child finishes its burst */
    waitpid(pid, NULL, 0);
}

/* ============================================================================
 * NON-PREEMPTIVE SCHEDULING — FCFS, SJF, Priority
 * ============================================================================
 * All three share the same simulation loop; only the "pick next process"
 * rule differs. Once a process starts, it runs to completion (no preemption).
 *
 * Waiting time per process = start_time - arrival_time
 * Idle time (CPU waiting for next arrival) does NOT count as any process's wait.
 */

#define POL_FCFS  0   /* pick earliest arrival among ready processes */
#define POL_SJF   1   /* pick shortest burst among ready processes */
#define POL_PRIO  2   /* pick lowest priority number among ready processes */

static void run_nonpreemptive(SchedProc procs[], int n, int policy,
                              const char *label)
{
    sched_header(label);

    int *done = calloc(n, sizeof(int));  /* 1 = process already completed */
    int t = 0;          /* simulated clock */
    int finished = 0;
    double wait_sum = 0.0;

    while (finished < n) {
        /* ---- SELECT: find the best ready process for this policy ---- */
        int best = -1;
        for (int i = 0; i < n; i++) {
            if (done[i] || procs[i].arrival > t) continue;  /* not ready yet */
            if (best < 0) { best = i; continue; }

            /* Tie-breaking: same scheduling key → lower index wins (CSV order) */
            int swap = 0;
            if (policy == POL_FCFS) {
                if (procs[i].arrival < procs[best].arrival)
                    swap = 1;
                else if (procs[i].arrival == procs[best].arrival && i < best)
                    swap = 1;
            } else if (policy == POL_SJF) {
                if (procs[i].burst < procs[best].burst)
                    swap = 1;
                else if (procs[i].burst == procs[best].burst && i < best)
                    swap = 1;
            } else {
                if (procs[i].priority < procs[best].priority)
                    swap = 1;
                else if (procs[i].priority == procs[best].priority && i < best)
                    swap = 1;
            }
            if (swap) best = i;
        }

        if (best < 0) {
            /* ---- IDLE: no process has arrived yet — fast-forward clock ---- */
            int earliest = -1;
            for (int i = 0; i < n; i++) {
                if (!done[i] && procs[i].arrival > t) {
                    if (earliest < 0 || procs[i].arrival < procs[earliest].arrival)
                        earliest = i;
                }
            }
            printf("%d → %d: Idle.\n", t, procs[earliest].arrival);
            fflush(stdout);
            wait_seconds(procs[earliest].arrival - t);
            t = procs[earliest].arrival;
            continue;
        }

        /* ---- RUN: execute selected process to completion ---- */
        int end = t + procs[best].burst;
        wait_sum += (double)(t - procs[best].arrival);

        printf("%d → %d: %s Running %s.\n",
               t, end, procs[best].name, procs[best].desc);
        fflush(stdout);

        sim_fork_and_wait(procs[best].burst);

        t = end;
        done[best] = 1;
        finished++;
    }

    sched_footer_avg(wait_sum / n);
    free(done);
}

/* ============================================================================
 * ROUND ROBIN SCHEDULING (preemptive)
 * ============================================================================
 *
 * PURPOSE: Each ready process gets at most `quantum` seconds per turn.
 * If it still has work left, SIGSTOP pauses the child and it goes to the
 * back of the ready queue. New arrivals are enqueued BEFORE the preempted
 * process returns — so a just-arrived process runs before a preempted one.
 *
 * WHY long-lived child processes (unlike non-preemptive)?
 *   RR must pause and resume the SAME process multiple times, so we fork
 *   each child once and control it with SIGSTOP / SIGCONT / SIGKILL.
 *
 * Total turnaround time = final clock value `t` when all processes finish.
 */

static void run_round_robin(SchedProc procs[], int n, int quantum) {
    sched_header("Round Robin");

    int   remaining[MAX_PROCS];  /* burst time left for each process */
    pid_t cpids[MAX_PROCS];      /* child PID for each process (-1 if not forked) */
    int   forked[MAX_PROCS];     /* 1 if we already forked this process's child */

    for (int i = 0; i < n; i++) {
        remaining[i] = procs[i].burst;
        cpids[i]     = -1;
        forked[i]    = 0;
    }

    IntQueue rq;   /* ready queue of process indices */
    iq_init(&rq);

    int t      = 0;       /* simulated clock */
    int next_p = 0;       /* index of next process not yet added to queue */
    int done   = 0;       /* count of completed processes */

    /* Seed the queue with all processes that arrive at time 0 */
    while (next_p < n && procs[next_p].arrival <= t) {
        iq_push(&rq, next_p);
        next_p++;
    }

    while (done < n) {

        if (iq_empty(&rq)) {
            /* CPU idle — jump forward to next arrival */
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

        /* Dequeue front of ready queue and run for one time slice */
        int idx   = iq_pop(&rq);
        int slice = (remaining[idx] < quantum) ? remaining[idx] : quantum;

        printf("%d → %d: %s Running %s.\n",
               t, t + slice, procs[idx].name, procs[idx].desc);
        fflush(stdout);

        /*
         * First time this process runs: fork a child that loops on pause().
         * Later times: send SIGCONT to resume the stopped child.
         */
        if (!forked[idx]) {
            cpids[idx] = fork();
            if (cpids[idx] < 0) { perror("fork"); exit(1); }
            if (cpids[idx] == 0) {
                while (1) pause();  /* child waits for parent signals */
                _exit(0);
            }
            forked[idx] = 1;
        } else {
            kill(cpids[idx], SIGCONT);
        }

        /* Parent waits for this time slice to elapse */
        wait_seconds(slice);

        t += slice;
        remaining[idx] -= slice;

        /* New arrivals join queue before preempted process goes back */
        while (next_p < n && procs[next_p].arrival <= t) {
            iq_push(&rq, next_p);
            next_p++;
        }

        if (remaining[idx] > 0) {
            /* Preempt: stop child and re-enqueue at the back */
            kill(cpids[idx], SIGSTOP);
            iq_push(&rq, idx);
        } else {
            /* Process finished: kill child and reap it */
            kill(cpids[idx], SIGKILL);
            waitpid(cpids[idx], NULL, 0);
            done++;
        }
    }

    sched_footer_rr(t);
}

/* ============================================================================
 * MAIN ENTRY POINT — called from ex3.c when argv[1] == "CPU-Scheduler"
 * ============================================================================
 *
 * @param processesCsvFilePath  Path to CSV file (argv[2])
 * @param timeQuantum           RR time quantum in seconds (argv[3])
 */

void runCPUScheduler(char *processesCsvFilePath, int timeQuantum) {

    /* Install SIGALRM handler — used by wait_seconds() to simulate time */
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags   = SA_RESTART;  /* restart interrupted syscalls after handler */
    sa.sa_handler = alrm_handler;
    sigaction(SIGALRM, &sa, NULL);

    /* Load processes and sort by arrival time (stable) */
    SchedProc procs[MAX_PROCS];
    int n = parse_csv(processesCsvFilePath, procs);

    qsort(procs, n, sizeof(SchedProc), cmp_arrival);

    /* Run all four algorithms in the required order on the same process set */
    run_nonpreemptive(procs, n, POL_FCFS, "FCFS");
    run_nonpreemptive(procs, n, POL_SJF,  "SJF");
    run_nonpreemptive(procs, n, POL_PRIO, "Priority");
    run_round_robin(procs, n, timeQuantum);
}
