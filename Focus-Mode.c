/*
 * ============================================================================
 * Focus-Mode.c — Focus Mode Simulator (Warmup, 20% of the grade)
 * ============================================================================
 *
 * GOAL:
 *   Practice Unix signal handling by simulating a "focus time" work session.
 *   Like the real Pomodoro / focus-time technique, you work with distractions
 *   blocked, then deal with everything that piled up when the round ends.
 *
 * WHAT THIS MODULE DOES:
 *   1. Registers handlers for 3 distraction signals (email, delivery, doorbell).
 *   2. Blocks those signals for the entire session ("Do Not Disturb" mode).
 *   3. Runs numOfRounds focus rounds; each round has up to `duration` user prompts.
 *   4. When the user picks a distraction (1/2/3), raise() sends a signal to
 *      ourselves — but because signals are blocked, they become PENDING instead
 *      of running their handler immediately.
 *   5. At the end of each round, sigpending() + sigismember() reveal which
 *      distractions are waiting; we deliver them one-by-one in fixed order (1→2→3).
 *
 * REQUIRED APIs (assignment enforces all four — missing any = 0 points):
 *   sigaction   — register signal handlers
 *   sigprocmask — block / unblock signals (the "focus" switch)
 *   sigpending  — ask the kernel which signals are waiting
 *   sigismember — test whether a specific signal is in the pending set
 *
 * SIGNAL MAPPING (you may choose any 3 signals except SIGKILL/SIGSTOP):
 *   SIGUSR1 → Email notification
 *   SIGUSR2 → Reminder to pick up delivery
 *   SIGHUP  → Doorbell ringing
 *
 * KEY CONCEPT — pending signals are NOT queued:
 *   If the user presses '1' three times in one round, only ONE SIGUSR1 is
 *   pending. Identical signals collapse to a single bit in the pending mask.
 * ============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

/* Width used to center round titles and separators in the output */
#define SEP_LEN 46

/*
 * Signal handlers — called when a blocked signal is finally delivered.
 *
 * PURPOSE: Print the "outcome" message for each distraction type.
 *
 * WHY write() instead of printf()?
 *   A handler can interrupt the process at any moment — even mid-printf.
 *   printf() uses internal buffers/locks and is NOT async-signal-safe.
 *   write() is safe to call from inside a signal handler.
 */

static void handle_email(int sig) {
    (void)sig;  /* unused parameter — required by signal handler signature */
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

/*
 * print_centered_symmetric — format helper for the assignment's expected output.
 * Centers a label (e.g. "Focus Round 1") inside SEP_LEN columns.
 */
static void print_centered_symmetric(const char *text) {
    int len = (int)strlen(text);
    int pad = (SEP_LEN - len) / 2;
    printf("%*s%s%*s\n", pad, "", text, pad, "");
}

/*
 * deliver_pending_signal — force one pending signal to run its handler.
 *
 * PURPOSE: We cannot call handlers directly; the kernel must deliver the signal.
 *   1. Temporarily UNBLOCK just this one signal.
 *   2. The kernel immediately delivers it → handler runs.
 *   3. Immediately RE-BLOCK it so later raises during the next round stay pending.
 *
 * This is the controlled "check your phone at break time" moment.
 */
static void deliver_pending_signal(int sig) {
    sigset_t temp;
    sigemptyset(&temp);
    sigaddset(&temp, sig);
    sigprocmask(SIG_UNBLOCK, &temp, NULL);  /* allow delivery */
    sigprocmask(SIG_BLOCK, &temp, NULL);    /* block again for next round */
}

/*
 * runFocusMode — main entry point called from ex3.c.
 *
 * @param numOfRounds  How many focus rounds to run (argv[2], e.g. 5).
 * @param duration     Max input prompts per round (argv[3], e.g. 3).
 *
 * One "focus round" = work phase (user input loop) + check phase (handle pending).
 */
void runFocusMode(int numOfRounds, int duration) {

    /* ------------------------------------------------------------------
     * SETUP: Register handlers with sigaction (preferred over signal()).
     * sigaction gives portable, reliable behavior across Unix systems.
     * ------------------------------------------------------------------ */
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);  /* don't block extra signals inside handlers */
    sa.sa_flags = 0;

    sa.sa_handler = handle_email;
    sigaction(SIGUSR1, &sa, NULL);

    sa.sa_handler = handle_delivery;
    sigaction(SIGUSR2, &sa, NULL);

    sa.sa_handler = handle_doorbell;
    sigaction(SIGHUP, &sa, NULL);

    /* ------------------------------------------------------------------
     * BLOCK all distraction signals for the whole session.
     * While blocked, raise() adds signals to the PENDING set instead of
     * invoking handlers. This is the core "focus mode" mechanism.
     * ------------------------------------------------------------------ */
    sigset_t block_set;
    sigemptyset(&block_set);
    sigaddset(&block_set, SIGUSR1);
    sigaddset(&block_set, SIGUSR2);
    sigaddset(&block_set, SIGHUP);
    sigprocmask(SIG_BLOCK, &block_set, NULL);

    printf("Entering Focus Mode. All distractions are blocked.\n");

    /* ------------------------------------------------------------------
     * OUTER LOOP — one iteration = one focus round.
     * ------------------------------------------------------------------ */
    for (int round = 1; round <= numOfRounds; round++) {
        printf("══════════════════════════════════════════════\n");

        char round_label[32];
        snprintf(round_label, sizeof(round_label), "Focus Round %d", round);
        print_centered_symmetric(round_label);

        printf("──────────────────────────────────────────────\n");

        /* --------------------------------------------------------------
         * WORK PHASE — up to `duration` prompts per round.
         * User simulates distractions; raise() sends signals to ourselves.
         * Because signals are blocked, they accumulate as pending.
         * 'q' ends the work phase early but the round still continues
         * to the check phase below.
         * Invalid input is ignored but still counts as one iteration.
         * -------------------------------------------------------------- */
        for (int i = 0; i < duration; i++) {
            printf("\nSimulate a distraction:\n");
            printf("  1 = Email notification\n");
            printf("  2 = Reminder to pick up delivery\n");
            printf("  3 = Doorbell Ringing\n");
            printf("  q = Quit\n");
            printf(">> ");
            fflush(stdout);

            char input[256];
            if (fgets(input, sizeof(input), stdin) == NULL)
                break;  /* EOF — treat like early quit */

            char ch = input[0];

            if (ch == 'q') {
                break;  /* end work phase; proceed to pending check */
            } else if (ch == '1') {
                raise(SIGUSR1);  /* equivalent to kill(getpid(), SIGUSR1) */
            } else if (ch == '2') {
                raise(SIGUSR2);
            } else if (ch == '3') {
                raise(SIGHUP);
            }
            /* any other character: ignored, but loop index advances */
        }

        /* --------------------------------------------------------------
         * CHECK PHASE — end of round: inspect and deliver pending signals.
         * Order is fixed: email (1) → delivery (2) → doorbell (3).
         * sigpending() fills `pending` with the kernel's pending mask.
         * sigismember() tests each distraction signal individually.
         * -------------------------------------------------------------- */
        printf("──────────────────────────────────────────────\n");
        printf("        Checking pending distractions...      \n");
        printf("──────────────────────────────────────────────\n");

        sigset_t pending;
        sigpending(&pending);

        int any_distraction = 0;

        if (sigismember(&pending, SIGUSR1)) {
            any_distraction = 1;
            printf(" - Email notification is waiting.\n");
            fflush(stdout);  /* ensure banner prints before handler's write() */
            deliver_pending_signal(SIGUSR1);
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

        if (!any_distraction) {
            printf("No distractions reached you this round.\n");
        }

        printf("──────────────────────────────────────────────\n");
        printf("             Back to Focus Mode.              \n");
        printf("══════════════════════════════════════════════\n");
    }

    /* ------------------------------------------------------------------
     * CLEANUP: Unblock all signals now that focus mode is over.
     * ------------------------------------------------------------------ */
    sigprocmask(SIG_UNBLOCK, &block_set, NULL);

    printf("\nFocus Mode complete. All distractions are now unblocked.\n");
}
