/*
 * sync.c
 * ------
 * Synchronization layer: Initialize/destroy mutex + condition variables,
 * and provide helper wrappers the scheduler uses to safely
 * grant/revoke CPU access to threads.
 *
 * WHY THIS IS NEEDED:
 *   Without mutex, two threads could both read ctx->current_pid == their
 *   PID at the same time and "run simultaneously" — violating the single-
 *   CPU model. The mutex ensures only one thread modifies shared state.
 */

#include "scheduler.h"

/* ─────────────────────────────────────────────────────────────────────
   INITIALIZE SCHEDULER CONTEXT
   Sets up mutex, condition variables, and zeroes out all fields.
   ───────────────────────────────────────────────────────────────────── */
void init_scheduler_context(SchedulerContext* ctx, PCB* processes, int count) {
    ctx->processes   = processes;
    ctx->count       = count;
    ctx->current_pid = -1;     /* -1 = CPU is idle / no process running */
    ctx->global_time = 0;
    ctx->time_slice  = TIME_QUANTUM;

    /* Initialize the mutex (binary lock for shared state) */
    if (pthread_mutex_init(&ctx->lock, NULL) != 0) {
        perror("pthread_mutex_init failed");
        exit(EXIT_FAILURE);
    }

    /* cpu_free: scheduler waits on this until current process is done */
    if (pthread_cond_init(&ctx->cpu_free, NULL) != 0) {
        perror("pthread_cond_init (cpu_free) failed");
        exit(EXIT_FAILURE);
    }

    /* proceed: broadcast to wake a specific process thread */
    if (pthread_cond_init(&ctx->proceed, NULL) != 0) {
        perror("pthread_cond_init (proceed) failed");
        exit(EXIT_FAILURE);
    }

    printf("  [Sync] Mutex and condition variables initialized.\n");
}

/* ─────────────────────────────────────────────────────────────────────
   DESTROY SCHEDULER CONTEXT
   Clean up resources after scheduling is done.
   ───────────────────────────────────────────────────────────────────── */
void destroy_scheduler_context(SchedulerContext* ctx) {
    pthread_mutex_destroy(&ctx->lock);
    pthread_cond_destroy(&ctx->cpu_free);
    pthread_cond_destroy(&ctx->proceed);
    printf("  [Sync] Resources freed.\n");
}

/* ─────────────────────────────────────────────────────────────────────
   GRANT CPU TO A PROCESS
   Scheduler calls this to hand CPU to a specific PID.
   Wakes all waiting threads; only the one with matching PID proceeds.
   ───────────────────────────────────────────────────────────────────── */
void grant_cpu(SchedulerContext* ctx, int pid) {
    pthread_mutex_lock(&ctx->lock);
    ctx->current_pid = pid;
    ctx->processes[pid].state = RUNNING;

    /* Broadcast wakes ALL waiting threads;
       each checks if ctx->current_pid == its own PID.
       Only the matching thread proceeds; others go back to sleep. */
    pthread_cond_broadcast(&ctx->proceed);
    pthread_mutex_unlock(&ctx->lock);
}

/* ─────────────────────────────────────────────────────────────────────
   WAIT FOR CPU TO BE RELEASED
   Scheduler blocks here until the current process yields / finishes.
   ───────────────────────────────────────────────────────────────────── */
void wait_for_cpu_release(SchedulerContext* ctx) {
    pthread_mutex_lock(&ctx->lock);

    /* Keep waiting until ctx->current_pid is cleared to -1 */
    while (ctx->current_pid != -1) {
        pthread_cond_wait(&ctx->cpu_free, &ctx->lock);
    }

    pthread_mutex_unlock(&ctx->lock);
}

/* ─────────────────────────────────────────────────────────────────────
   RESET PROCESSES FOR A NEW ALGORITHM RUN
   Restores remaining_time and clears stats so we can re-run.
   ───────────────────────────────────────────────────────────────────── */
void reset_processes(PCB* processes, int count) {
    for (int i = 0; i < count; i++) {
        processes[i].remaining_time   = processes[i].burst_time;
        processes[i].waiting_time     = 0;
        processes[i].turnaround_time  = 0;
        processes[i].completion_time  = 0;
        processes[i].state            = NEW;
        processes[i].done             = 0;
        processes[i].scheduled        = 0;
        processes[i].thread           = 0;
    }
}
