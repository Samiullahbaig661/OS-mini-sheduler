/*
 * scheduler.c
 * -----------
 * Implements three CPU scheduling algorithms.
 * Each algorithm controls which thread (process) gets CPU access
 * by setting ctx->current_pid and using condition variables to
 * wake that specific thread.
 *
 * ALGORITHMS:
 *   1. FCFS  — First Come First Served (non-preemptive)
 *   2. SJF   — Shortest Job First (non-preemptive)
 *   3. Round Robin — Preemptive, fixed time quantum
 *
 * FLOW FOR EACH:
 *   spawn threads → sort/select → grant CPU → wait for yield → repeat
 */

#include "scheduler.h"

/* Macro to stringify TIME_QUANTUM */
#define STRINGIFY(x) #x
#define TOSTRING(x)  STRINGIFY(x)

/* Forward declarations for internal helpers */
static int  all_done(SchedulerContext* ctx);
static int  get_next_arrived(SchedulerContext* ctx, int current_time);
static int  get_shortest_job(SchedulerContext* ctx, int current_time);
static void print_schedule_header(const char* algo);

/* sync.c functions used here */
void grant_cpu(SchedulerContext* ctx, int pid);
void wait_for_cpu_release(SchedulerContext* ctx);

/* ─────────────────────────────────────────────────────────────────────
   GANTT CHART DATA  (recorded during scheduling)
   ───────────────────────────────────────────────────────────────────── */
typedef struct {
    int pid;
    int start;
    int end;
} GanttEntry;

static GanttEntry gantt[100];
static int        gantt_idx = 0;

/* ═════════════════════════════════════════════════════════════════════
   ALGORITHM 1: FIRST COME FIRST SERVED (FCFS)
   ═════════════════════════════════════════════════════════════════════
   - Processes are scheduled in order of arrival_time.
   - Non-preemptive: once a process starts, it runs to completion.
   - Simple but can cause "convoy effect" (long jobs block short ones).
   ───────────────────────────────────────────────────────────────────── */
void run_fcfs(SchedulerContext* ctx) {
    print_schedule_header("FCFS — First Come First Served");
    gantt_idx = 0;

    /* Spawn all threads — they will block waiting for CPU */
    create_process_threads(ctx);

    int time = 0;

    /* Sort by arrival time (simple insertion sort) */
    for (int i = 0; i < ctx->count - 1; i++) {
        for (int j = i + 1; j < ctx->count; j++) {
            if (ctx->processes[j].arrival_time < ctx->processes[i].arrival_time) {
                PCB tmp = ctx->processes[i];
                ctx->processes[i] = ctx->processes[j];
                ctx->processes[j] = tmp;
            }
        }
    }

    for (int i = 0; i < ctx->count; i++) {
        PCB* p = &ctx->processes[i];

        /* If CPU is idle before this process arrives, advance time */
        if (time < p->arrival_time) {
            printf("\n  [FCFS] CPU idle from %d to %d\n", time, p->arrival_time);
            time = p->arrival_time;
            ctx->global_time = time;
        }

        printf("\n  [FCFS] Scheduling %s for %d units\n",
               p->name, p->remaining_time);

        int start_time = time;

        /* Run this process for its full burst (non-preemptive) */
        while (p->remaining_time > 0) {
            /* Grant CPU and wait for 1 unit of work */
            grant_cpu(ctx, p->pid);
            wait_for_cpu_release(ctx);
            time++;
        }

        /* Record in Gantt chart */
        gantt[gantt_idx].pid   = p->pid;
        gantt[gantt_idx].start = start_time;
        gantt[gantt_idx].end   = time;
        gantt_idx++;
    }

    wait_for_all_threads(ctx);
    calculate_metrics(ctx->processes, ctx->count);
    print_results(ctx->processes, ctx->count, "FCFS");
    print_gantt_chart(ctx->processes, ctx->count);
}

/* ═════════════════════════════════════════════════════════════════════
   ALGORITHM 2: SHORTEST JOB FIRST (SJF)
   ═════════════════════════════════════════════════════════════════════
   - At each scheduling point, pick the process with the smallest
     burst_time among all READY (arrived) processes.
   - Non-preemptive: once selected, runs to completion.
   - Optimal for minimizing average waiting time.
   ───────────────────────────────────────────────────────────────────── */
void run_sjf(SchedulerContext* ctx) {
    print_schedule_header("SJF — Shortest Job First (Non-Preemptive)");
    gantt_idx = 0;

    create_process_threads(ctx);

    int time = 0;
    int completed = 0;

    while (completed < ctx->count) {
        /* Find shortest job that has arrived by current time */
        int idx = get_shortest_job(ctx, time);

        if (idx == -1) {
            /* No process ready yet — CPU idles for 1 unit */
            printf("  [SJF] CPU idle at time %d\n", time);
            sleep(1);
            time++;
            ctx->global_time = time;
            continue;
        }

        PCB* p = &ctx->processes[idx];
        printf("\n  [SJF] Scheduling %s (burst=%d) at time %d\n",
               p->name, p->burst_time, time);

        int start_time = time;

        /* Run to completion */
        while (p->remaining_time > 0) {
            grant_cpu(ctx, p->pid);
            wait_for_cpu_release(ctx);
            time++;
        }

        gantt[gantt_idx].pid   = p->pid;
        gantt[gantt_idx].start = start_time;
        gantt[gantt_idx].end   = time;
        gantt_idx++;

        completed++;
    }

    wait_for_all_threads(ctx);
    calculate_metrics(ctx->processes, ctx->count);
    print_results(ctx->processes, ctx->count, "SJF");
    print_gantt_chart(ctx->processes, ctx->count);
}

/* ═════════════════════════════════════════════════════════════════════
   ALGORITHM 3: ROUND ROBIN (RR)
   ═════════════════════════════════════════════════════════════════════
   - Each process gets a fixed TIME_QUANTUM on the CPU.
   - If not finished, it goes back to the end of the ready queue.
   - Preemptive: scheduler takes CPU away after quantum expires.
   - Fair: every process gets regular CPU time.
   ───────────────────────────────────────────────────────────────────── */
void run_round_robin(SchedulerContext* ctx) {
    print_schedule_header("Round Robin (Preemptive, Quantum = " 
                          TOSTRING(TIME_QUANTUM) " sec)");
    gantt_idx = 0;

    create_process_threads(ctx);

    /* Build a simple ready queue (circular buffer using array + indices) */
    int queue[MAX_PROCESSES * 20];   /* Large enough for multiple cycles  */
    int head = 0, tail = 0;
    int time = 0;
    int completed = 0;
    int enqueued[MAX_PROCESSES] = {0};

    /* Enqueue all processes that arrive at time 0 */
    for (int i = 0; i < ctx->count; i++) {
        if (ctx->processes[i].arrival_time == 0) {
            queue[tail++] = i;
            enqueued[i] = 1;
        }
    }

    while (completed < ctx->count) {
        /* Enqueue any newly arrived processes */
        for (int i = 0; i < ctx->count; i++) {
            if (!enqueued[i] &&
                ctx->processes[i].arrival_time <= time &&
                !ctx->processes[i].done) {
                queue[tail++] = i;
                enqueued[i]   = 1;
            }
        }

        if (head == tail) {
            /* No process ready — idle */
            printf("  [RR] CPU idle at time %d\n", time);
            sleep(1);
            time++;
            ctx->global_time = time;
            continue;
        }

        int idx = queue[head++];
        PCB* p  = &ctx->processes[idx];

        if (p->done) continue;  /* Skip already-finished processes */

        printf("\n  [RR] Scheduling %s | remaining=%d | quantum=%d\n",
               p->name, p->remaining_time, TIME_QUANTUM);

        int start_time = time;
        int ran_for    = 0;

        /* Run for up to TIME_QUANTUM units */
        while (p->remaining_time > 0 && ran_for < TIME_QUANTUM) {
            grant_cpu(ctx, p->pid);
            wait_for_cpu_release(ctx);
            time++;
            ran_for++;
        }

        gantt[gantt_idx].pid   = p->pid;
        gantt[gantt_idx].start = start_time;
        gantt[gantt_idx].end   = time;
        gantt_idx++;

        if (p->done) {
            completed++;
        } else {
            /* Process not finished — re-enqueue */
            queue[tail++] = idx;
            printf("  [RR] %s preempted after %d units, re-queued\n",
                   p->name, ran_for);
        }
    }

    wait_for_all_threads(ctx);
    calculate_metrics(ctx->processes, ctx->count);
    print_results(ctx->processes, ctx->count, "Round Robin");
    print_gantt_chart(ctx->processes, ctx->count);
}

/* ═════════════════════════════════════════════════════════════════════
   METRICS CALCULATION
   WT  = Completion Time - Arrival Time - Burst Time
   TAT = Completion Time - Arrival Time
   ═════════════════════════════════════════════════════════════════════ */
void calculate_metrics(PCB* processes, int count) {
    for (int i = 0; i < count; i++) {
        processes[i].turnaround_time =
            processes[i].completion_time - processes[i].arrival_time;

        processes[i].waiting_time =
            processes[i].turnaround_time - processes[i].burst_time;

        /* Clamp to 0 (floating-point timing may cause -1) */
        if (processes[i].waiting_time < 0)
            processes[i].waiting_time = 0;
    }
}

/* ═════════════════════════════════════════════════════════════════════
   RESULTS TABLE
   ═════════════════════════════════════════════════════════════════════ */
void print_results(PCB* processes, int count, const char* algo_name) {
    float total_wt  = 0, total_tat = 0;

    printf("\n");
    print_separator();
    printf("  RESULTS: %s\n", algo_name);
    print_separator();
    printf("  %-8s %-10s %-10s %-12s %-12s %-12s\n",
           "Process", "Arrival", "Burst", "Completion", "Waiting", "Turnaround");
    printf("  %-8s %-10s %-10s %-12s %-12s %-12s\n",
           "-------", "-------", "-----", "----------", "-------", "----------");

    for (int i = 0; i < count; i++) {
        printf("  %-8s %-10d %-10d %-12d %-12d %-12d\n",
               processes[i].name,
               processes[i].arrival_time,
               processes[i].burst_time,
               processes[i].completion_time,
               processes[i].waiting_time,
               processes[i].turnaround_time);

        total_wt  += processes[i].waiting_time;
        total_tat += processes[i].turnaround_time;
    }

    printf("  %-8s %-10s %-10s %-12s %-12s %-12s\n",
           "-------", "-------", "-----", "----------", "-------", "----------");
    printf("\n  Average Waiting Time    : %.2f sec\n", total_wt  / count);
    printf("  Average Turnaround Time : %.2f sec\n", total_tat / count);
    print_separator();
}

/* ═════════════════════════════════════════════════════════════════════
   GANTT CHART (text-based visual timeline)
   ═════════════════════════════════════════════════════════════════════ */
void print_gantt_chart(PCB* processes, int count) {
    (void)processes; (void)count;   /* suppress unused-param warnings */

    printf("\n  GANTT CHART:\n  ");

    for (int i = 0; i < gantt_idx; i++) {
        int duration = gantt[i].end - gantt[i].start;
        /* Print block with process label */
        printf("|");
        for (int d = 0; d < duration; d++) {
            if (d == duration / 2)
                printf("P%d", gantt[i].pid +1);
            else
                printf("-");
        }
    }
    printf("|\n  ");

    /* Print time labels below */
    int t = gantt[0].start;
    printf("%d", t);
    for (int i = 0; i < gantt_idx; i++) {
        int duration = gantt[i].end - gantt[i].start;
        for (int d = 0; d < duration; d++) printf(" ");
        t = gantt[i].end;
        printf("%d", t);
    }
    printf("\n\n");
}

/* ─────────────────────────────────────────────────────────────────────
   INTERNAL HELPERS
   ───────────────────────────────────────────────────────────────────── */
static int all_done(SchedulerContext* ctx) {
    for (int i = 0; i < ctx->count; i++)
        if (!ctx->processes[i].done) return 0;
    return 1;
}

static int get_next_arrived(SchedulerContext* ctx, int current_time) {
    int idx = -1;
    for (int i = 0; i < ctx->count; i++) {
        PCB* p = &ctx->processes[i];
        if (!p->done && p->arrival_time <= current_time) {
            if (idx == -1 ||
                p->arrival_time < ctx->processes[idx].arrival_time)
                idx = i;
        }
    }
    return idx;
}

static int get_shortest_job(SchedulerContext* ctx, int current_time) {
    int idx = -1;
    for (int i = 0; i < ctx->count; i++) {
        PCB* p = &ctx->processes[i];
        if (!p->done && p->arrival_time <= current_time) {
            if (idx == -1 ||
                p->burst_time < ctx->processes[idx].burst_time)
                idx = i;
        }
    }
    return idx;
}

static void print_schedule_header(const char* algo) {
    printf("\n");
    print_separator();
    printf("  ALGORITHM: %s\n", algo);
    print_separator();
    printf("\n");
}
