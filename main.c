/*
 * main.c
 * ------
 * Program entry point.
 * Handles user input, displays the menu, and coordinates the simulation.
 *
 * FLOW:
 *   1. Display banner
 *   2. Get process info from user (or use demo data)
 *   3. Show algorithm menu
 *   4. Initialize SchedulerContext (mutex, cond vars)
 *   5. Call the chosen scheduling algorithm
 *   6. Clean up and optionally run another algorithm
 */

#include "scheduler.h"

/* ─────────────────────────────────────────────────────────────────────
   UTILITY: BANNER & SEPARATOR
   ───────────────────────────────────────────────────────────────────── */
void print_banner(void) {
    printf("\n");
    printf("  ╔══════════════════════════════════════════════════╗\n");
    printf("  ║       OS PROCESS SCHEDULER SIMULATOR             ║\n");
    printf("  ║   Using POSIX Threads (pthreads) + Mutex/CV      ║\n");
    printf("  ║   Algorithms: FCFS | SJF | Round Robin           ║\n");
    printf("  ╚══════════════════════════════════════════════════╝\n");
    printf("\n");
}

void print_separator(void) {
    printf("  ──────────────────────────────────────────────────────\n");
}

/* ─────────────────────────────────────────────────────────────────────
   PRINT PROCESS TABLE (before scheduling)
   ───────────────────────────────────────────────────────────────────── */
void print_process_table(PCB* processes, int count) {
    printf("\n");
    print_separator();
    printf("  PROCESS TABLE\n");
    print_separator();
    printf("  %-8s %-12s %-10s %-10s\n",
           "Process", "Arrival(s)", "Burst(s)", "Priority");
    printf("  %-8s %-12s %-10s %-10s\n",
           "-------", "----------", "--------", "--------");
    for (int i = 0; i < count; i++) {
        printf("  %-8s %-12d %-10d %-10d\n",
               processes[i].name,
               processes[i].arrival_time,
               processes[i].burst_time,
               processes[i].priority);
    }
    print_separator();
    printf("\n");
}

/* ─────────────────────────────────────────────────────────────────────
   LOAD DEMO PROCESSES (so user can skip manual input)
   ───────────────────────────────────────────────────────────────────── */
static void load_demo_processes(PCB* processes, int* count) {
    *count = 4;

    /* PID 0 */
    processes[0].pid          = 0;
    strcpy(processes[0].name, "P1");
    processes[0].arrival_time = 0;
    processes[0].burst_time   = 4;
    processes[0].priority     = 2;

    /* PID 1 */
    processes[1].pid          = 1;
    strcpy(processes[1].name, "P2");
    processes[1].arrival_time = 1;
    processes[1].burst_time   = 3;
    processes[1].priority     = 1;

    /* PID 2 */
    processes[2].pid          = 2;
    strcpy(processes[2].name, "P3");
    processes[2].arrival_time = 2;
    processes[2].burst_time   = 2;
    processes[2].priority     = 3;

    /* PID 3 */
    processes[3].pid          = 3;
    strcpy(processes[3].name, "P4");
    processes[3].arrival_time = 3;
    processes[3].burst_time   = 5;
    processes[3].priority     = 2;

    /* Initialize remaining fields */
    for (int i = 0; i < *count; i++) {
        processes[i].remaining_time  = processes[i].burst_time;
        processes[i].waiting_time    = 0;
        processes[i].turnaround_time = 0;
        processes[i].completion_time = 0;
        processes[i].state           = NEW;
        processes[i].done            = 0;
        processes[i].scheduled       = 0;
        processes[i].thread          = 0;
    }

    printf("  Demo processes loaded:\n");
    printf("    P1: arrival=0, burst=4\n");
    printf("    P2: arrival=1, burst=3\n");
    printf("    P3: arrival=2, burst=2\n");
    printf("    P4: arrival=3, burst=5\n");
}

/* ─────────────────────────────────────────────────────────────────────
   GET PROCESS INPUT FROM USER
   ───────────────────────────────────────────────────────────────────── */
void get_process_input(PCB* processes, int* count) {
    printf("  How many processes? (max %d): ", MAX_PROCESSES);
    scanf("%d", count);

    if (*count < 1 || *count > MAX_PROCESSES) {
        printf("  Invalid count. Using 3 demo processes.\n");
        load_demo_processes(processes, count);
        return;
    }

    for (int i = 0; i < *count; i++) {
        processes[i].pid = i;
        snprintf(processes[i].name, sizeof(processes[i].name), "P%d", i + 1);

        printf("\n  Process P%d:\n", i + 1);
        printf("    Arrival time (sec): ");
        scanf("%d", &processes[i].arrival_time);

        printf("    Burst time   (sec): ");
        scanf("%d", &processes[i].burst_time);
        if (processes[i].burst_time < 1) processes[i].burst_time = 1;

        printf("    Priority (1=highest): ");
        scanf("%d", &processes[i].priority);

        /* Initialize runtime fields */
        processes[i].remaining_time  = processes[i].burst_time;
        processes[i].waiting_time    = 0;
        processes[i].turnaround_time = 0;
        processes[i].completion_time = 0;
        processes[i].state           = NEW;
        processes[i].done            = 0;
        processes[i].scheduled       = 0;
        processes[i].thread          = 0;
    }
}

/* ─────────────────────────────────────────────────────────────────────
   ALGORITHM SELECTION MENU
   ───────────────────────────────────────────────────────────────────── */
static int show_algorithm_menu(void) {
    int choice;
    printf("\n");
    print_separator();
    printf("  SELECT SCHEDULING ALGORITHM\n");
    print_separator();
    printf("  1. FCFS   — First Come First Served\n");
    printf("  2. SJF    — Shortest Job First\n");
    printf("  3. RR     — Round Robin (Quantum = %d sec)\n", TIME_QUANTUM);
    printf("  4. Run ALL algorithms (compare)\n");
    printf("  5. Exit\n");
    print_separator();
    printf("  Choice: ");
    scanf("%d", &choice);
    return choice;
}

/* ─────────────────────────────────────────────────────────────────────
   RUN ONE ALGORITHM (handles init + reset + destroy cycle)
   ───────────────────────────────────────────────────────────────────── */
static void run_algorithm(int choice,
                           PCB* processes, int count,
                           PCB* backup, SchedulerContext* ctx) {

    /* Restore original process data from backup */
    memcpy(processes, backup, sizeof(PCB) * count);
    reset_processes(processes, count);

    /* Initialize synchronization primitives */
    init_scheduler_context(ctx, processes, count);

    switch (choice) {
        case 1: run_fcfs(ctx);        break;
        case 2: run_sjf(ctx);         break;
        case 3: run_round_robin(ctx); break;
        default:
            printf("  Unknown algorithm choice.\n");
    }

    /* Clean up mutex/cond vars */
    destroy_scheduler_context(ctx);

    printf("\n  Press ENTER to continue...");
    getchar(); getchar();
}

/* ─────────────────────────────────────────────────────────────────────
   MAIN
   ───────────────────────────────────────────────────────────────────── */
int main(void) {
    PCB processes[MAX_PROCESSES];
    PCB backup[MAX_PROCESSES];      /* Backup to restore for each algo */
    SchedulerContext ctx;
    int count = 0;
    int choice;

    print_banner();

    /* ── INPUT CHOICE ──────────────────────────────────────────────── */
    printf("  Use demo processes? (1=Yes / 0=No): ");
    int use_demo;
    scanf("%d", &use_demo);

    if (use_demo) {
        load_demo_processes(processes, &count);
    } else {
        get_process_input(processes, &count);
    }

    print_process_table(processes, count);

    /* Save a backup of original process data */
    memcpy(backup, processes, sizeof(PCB) * count);

    /* ── MAIN MENU LOOP ─────────────────────────────────────────────── */
    while (1) {
        choice = show_algorithm_menu();

        if (choice == 5) {
            printf("\n  Goodbye!\n\n");
            break;
        }

        if (choice == 4) {
            /* Run all three and compare */
            printf("\n  ===== Running ALL algorithms =====\n");

            for (int algo = 1; algo <= 3; algo++) {
                run_algorithm(algo, processes, count, backup, &ctx);
                sleep(1);
            }
        } else if (choice >= 1 && choice <= 3) {
            run_algorithm(choice, processes, count, backup, &ctx);
        } else {
            printf("  Invalid choice. Try again.\n");
        }
    }

    return 0;
}
