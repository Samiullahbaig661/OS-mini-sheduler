/*
 * thread_manager.c
 * ----------------
 * Each PROCESS is simulated as a real pthread thread.
 *
 * HOW IT WORKS:
 *   1. Scheduler calls create_process_threads() → spawns N threads.
 *   2. Each thread starts process_thread_fn() and IMMEDIATELY blocks,
 *      waiting for the scheduler to give it CPU permission.
 *   3. The scheduler sets ctx->current_pid = this_pid and broadcasts.
 *   4. The thread wakes up, "executes" (sleeps 1 sec = 1 CPU burst unit),
 *      then blocks again — simulating preemption.
 *   5. When remaining_time == 0, thread marks itself FINISHED and exits.
 *
 * This is meaningful because:
 *   - Threads really are created and managed by the OS (Linux pthreads).
 *   - Mutual exclusion (mutex) prevents two threads running simultaneously.
 *   - Condition variables simulate context switching.
 */

#include "scheduler.h"

/* ─────────────────────────────────────────────────────────────────────
   THREAD ENTRY FUNCTION
   Every process thread starts here and loops until its burst is done.
   ───────────────────────────────────────────────────────────────────── */
void* process_thread_fn(void* arg) {
    ThreadArg*        targ = (ThreadArg*) arg;
    PCB*              pcb  = targ->pcb;
    SchedulerContext* ctx  = targ->ctx;

    printf("  [Thread Created] %s (PID %d) | Burst: %d sec | Arrival: %d sec\n",
           pcb->name, pcb->pid, pcb->burst_time, pcb->arrival_time);

    /* ── ARRIVAL DELAY ──────────────────────────────────────────────
       Simulate process arriving at its arrival_time.
       Thread simply sleeps until its arrival time passes.
       ────────────────────────────────────────────────────────────── */
    if (pcb->arrival_time > 0) {
        sleep(pcb->arrival_time);
    }

    pthread_mutex_lock(&ctx->lock);
    pcb->state = READY;
    printf("  [%s] Arrived — now READY (global time ~%d)\n",
           pcb->name, ctx->global_time);
    pthread_mutex_unlock(&ctx->lock);

    /* ── MAIN EXECUTION LOOP ────────────────────────────────────────
       Thread keeps waiting for CPU permission.
       When ctx->current_pid == our PID, we run for 1 time unit.
       ────────────────────────────────────────────────────────────── */
    while (1) {
        pthread_mutex_lock(&ctx->lock);

        /* Wait until scheduler selects THIS process */
        while (ctx->current_pid != pcb->pid) {
            if (pcb->done) {
                pthread_mutex_unlock(&ctx->lock);
                free(targ);
                pthread_exit(NULL);
            }
            /* Block here — release lock and wait for signal */
            pcb->state = WAITING;
            pthread_cond_wait(&ctx->proceed, &ctx->lock);
        }

        /* ── CPU GRANTED ─────────────────────────────────────────── */
        pcb->state = RUNNING;
        pcb->scheduled = 1;
        printf("  [%s] Running... (remaining: %d sec) [PID:%d | Thread:%lu]\n",
               pcb->name,
               pcb->remaining_time,
               pcb->pid,
               (unsigned long)pcb->thread);

        pthread_mutex_unlock(&ctx->lock);

        /* Simulate 1 unit of CPU work (real sleep = real OS scheduling) */
        sleep(1);

        /* ── UPDATE STATE AFTER EXECUTION ────────────────────────── */
        pthread_mutex_lock(&ctx->lock);

        pcb->remaining_time--;
        ctx->global_time++;

        if (pcb->remaining_time <= 0) {
            /* Process FINISHED */
            pcb->completion_time = ctx->global_time;
            pcb->state           = FINISHED;
            pcb->done            = 1;
            ctx->current_pid     = -1;   /* Release CPU */

            printf("  [%s] ✓ FINISHED at time %d\n",
                   pcb->name, pcb->completion_time);

            /* Wake up scheduler so it picks next process */
            pthread_cond_broadcast(&ctx->cpu_free);
            pthread_mutex_unlock(&ctx->lock);
            free(targ);
            pthread_exit(NULL);
        } else {
            /* Still has work — give CPU back to scheduler */
            ctx->current_pid = -1;
            pthread_cond_broadcast(&ctx->cpu_free);
        }

        pthread_mutex_unlock(&ctx->lock);
    }

    free(targ);
    return NULL;
}

/* ─────────────────────────────────────────────────────────────────────
   CREATE ALL PROCESS THREADS
   Called once at the start of each scheduling algorithm.
   ───────────────────────────────────────────────────────────────────── */
void create_process_threads(SchedulerContext* ctx) {
    printf("\n  Spawning %d process threads...\n\n", ctx->count);

    for (int i = 0; i < ctx->count; i++) {
        PCB* pcb = &ctx->processes[i];

        /* Reset for fresh run */
        pcb->state          = NEW;
        pcb->done           = 0;
        pcb->scheduled      = 0;

        /* Allocate thread argument (freed inside thread when done) */
        ThreadArg* arg = malloc(sizeof(ThreadArg));
        if (!arg) {
            perror("malloc failed for ThreadArg");
            exit(EXIT_FAILURE);
        }
        arg->pcb = pcb;
        arg->ctx = ctx;

        /* Create the thread — this is where the real OS thread is born */
        int ret = pthread_create(&pcb->thread, NULL, process_thread_fn, arg);
        if (ret != 0) {
            fprintf(stderr, "pthread_create failed for %s: %s\n",
                    pcb->name, strerror(ret));
            exit(EXIT_FAILURE);
        }
    }

    /* Small delay so all threads have time to initialize */
    sleep(1);
}

/* ─────────────────────────────────────────────────────────────────────
   WAIT FOR ALL THREADS TO COMPLETE
   Scheduler calls this after all processes are dispatched.
   ───────────────────────────────────────────────────────────────────── */
void wait_for_all_threads(SchedulerContext* ctx) {
    for (int i = 0; i < ctx->count; i++) {
        /* Only join if the thread was actually created */
        if (ctx->processes[i].thread != 0) {
            pthread_join(ctx->processes[i].thread, NULL);
        }
    }
    printf("\n  All threads have completed execution.\n");
}
