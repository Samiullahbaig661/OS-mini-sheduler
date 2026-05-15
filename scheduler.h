/*
 * scheduler.h
 * -----------
 * Header file: All data structures, constants, and function declarations
 * for the OS Scheduler simulation project.
 *
 * Each "process" in this simulation is a REAL pthread thread.
 * The scheduler controls which thread runs using mutex + condition variables.
 */

#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

/* ─────────────────────────────────────────
   CONSTANTS
   ───────────────────────────────────────── */
#define MAX_PROCESSES     10
#define TIME_QUANTUM      2      /* Round Robin quantum (seconds) */
#define IDLE_SYMBOL       "░"
#define RUNNING_SYMBOL    "█"

/* ─────────────────────────────────────────
   PROCESS STATES
   ───────────────────────────────────────── */
typedef enum {
    NEW      = 0,
    READY    = 1,
    RUNNING  = 2,
    WAITING  = 3,
    FINISHED = 4
} ProcessState;

static const char* state_names[] = {
    "NEW", "READY", "RUNNING", "WAITING", "FINISHED"
};

/* ─────────────────────────────────────────
   PROCESS CONTROL BLOCK (PCB)
   ───────────────────────────────────────── */
typedef struct {
    int           pid;              /* Process ID                        */
    char          name[20];         /* Process name e.g. "P1"            */
    int           arrival_time;     /* When process arrives (sec)        */
    int           burst_time;       /* Total CPU time needed (sec)       */
    int           remaining_time;   /* Time left to finish               */
    int           priority;         /* Priority (lower = higher priority)*/
    int           waiting_time;     /* WT = completion - arrival - burst */
    int           turnaround_time;  /* TAT = completion - arrival        */
    int           completion_time;  /* When process finished             */
    ProcessState  state;            /* Current state of process          */

    /* Thread fields — each process IS a thread */
    pthread_t     thread;           /* The actual OS thread              */
    int           scheduled;        /* Flag: 1 = scheduler gave CPU      */
    int           done;             /* Flag: 1 = thread finished work    */
} PCB;

/* ─────────────────────────────────────────
   SHARED SCHEDULER CONTEXT
   (Passed to every thread so they can
    coordinate with the scheduler)
   ───────────────────────────────────────── */
typedef struct {
    PCB*            processes;      /* Array of all PCBs                 */
    int             count;          /* Total number of processes         */
    int             current_pid;    /* PID of process allowed to run     */
    int             time_slice;     /* Remaining time slice for RR       */
    int             global_time;    /* Simulated global clock            */

    pthread_mutex_t lock;           /* Mutex to protect shared state     */
    pthread_cond_t  cpu_free;       /* Signal: CPU is free               */
    pthread_cond_t  proceed;        /* Signal: this thread may proceed   */
} SchedulerContext;

/* ─────────────────────────────────────────
   SCHEDULER FUNCTION DECLARATIONS
   ───────────────────────────────────────── */
void run_fcfs(SchedulerContext* ctx);
void run_sjf(SchedulerContext* ctx);
void run_round_robin(SchedulerContext* ctx);

void calculate_metrics(PCB* processes, int count);
void print_results(PCB* processes, int count, const char* algo_name);
void print_gantt_chart(PCB* processes, int count);

/* ─────────────────────────────────────────
   THREAD MANAGER FUNCTION DECLARATIONS
   ───────────────────────────────────────── */
void  create_process_threads(SchedulerContext* ctx);
void  wait_for_all_threads(SchedulerContext* ctx);
void* process_thread_fn(void* arg);     /* Thread entry function         */

/* Argument struct passed to each thread */
typedef struct {
    PCB*             pcb;           /* Pointer to this process's PCB     */
    SchedulerContext* ctx;          /* Pointer to shared context         */
} ThreadArg;

/* ─────────────────────────────────────────
   SYNC / UTILITY FUNCTION DECLARATIONS
   ───────────────────────────────────────── */
void init_scheduler_context(SchedulerContext* ctx, PCB* processes, int count);
void destroy_scheduler_context(SchedulerContext* ctx);
void reset_processes(PCB* processes, int count);   /* Reset for re-run  */
void grant_cpu(SchedulerContext* ctx, int pid);
void wait_for_cpu_release(SchedulerContext* ctx);

/* ─────────────────────────────────────────
   INPUT / DISPLAY HELPERS
   ───────────────────────────────────────── */
void  get_process_input(PCB* processes, int* count);
void  print_process_table(PCB* processes, int count);
void  print_banner(void);
void  print_separator(void);

#endif /* SCHEDULER_H */
