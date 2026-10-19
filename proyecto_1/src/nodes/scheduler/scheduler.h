#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdint.h>
#include <stdbool.h>
#include "task.h"

#define MAX_TASKS 20
#define MAX_JOBS 40

// ---------------------- SchedulerType ----------------------

/*
 * Defines the scheduling algorithm used to select the next job.
 * RMS uses fixed priorities based on task periods, while EDF selects
 * the active job with the earliest absolute deadline.
 */
typedef enum {
    RMS,
    EDF
} SchedulerType;

// ---------------------- Scheduler ----------------------

/*
 * Stores the scheduler configuration, registered tasks, active jobs,
 * and available job slots. It also tracks the current execution tick,
 * deadline misses, and response-time statistics for sporadic tasks.
 */
typedef struct Scheduler {
    SchedulerType type;

    Task tasks[MAX_TASKS]; // Tasks assigned by the host.
    Job jobs[MAX_JOBS];    // Storage for active jobs.

    uint32_t free_index[MAX_JOBS];
    uint32_t free_count;   // Number of available job slots.

    uint32_t task_count;   // Number of registered tasks.
    uint32_t job_count;    // Number of active jobs.

    uint32_t current_tick; // Current scheduler tick.
    uint32_t sn;           // Maximum number of UTs executed per tick.

    // Stores whether a deadline miss occurred and information about the
    // most recent missed job.
    bool deadline_missed;
    uint32_t deadline_miss_count;
    uint32_t last_missed_task_id;
    uint32_t last_missed_deadline;

    // Tracks the registered sporadic task, its release interval, and
    // the number of rejected events and completed jobs.
    uint32_t sporadic_task_index;
    uint32_t sporadic_last_release_tick;
    uint32_t sporadic_rejected_count;
    uint32_t sporadic_completed_count;
    uint64_t sporadic_total_response_ticks;
    bool sporadic_has_released;

    // Stores the last, minimum, and maximum response times measured
    // for completed sporadic jobs.
    uint32_t sporadic_last_response_ticks;
    uint32_t sporadic_min_response_ticks;
    uint32_t sporadic_max_response_ticks;

} Scheduler;

// ---------------------- Function declarations ----------------------

/*
 * Initializes the scheduler, configures its scheduling algorithm and
 * execution capacity, and resets its counters and internal job storage.
 */
void init(Scheduler *sc, uint32_t sn, SchedulerType type);

/*
 * Registers a periodic or sporadic task after validating its parameters
 * and checking that the scheduler has enough capacity.
 */
bool add_task(Task *task, Scheduler *sc);

/*
 * Retrieves an available job slot from the free-index list.
 * Returns -1 if no job slots are available.
 */
int get_free_index(Scheduler *sc);

/*
 * Deactivates a job and returns its storage slot to the free-index list
 * so that it can be reused by a future job.
 */
bool free_job(Scheduler *sc, uint32_t index);

/*
 * Creates a new job from a registered task and initializes its release
 * tick, absolute deadline, remaining workload, and active status.
 */
bool release_job(Scheduler *sc, uint32_t task_index);

/*
 * Checks registered periodic tasks and releases jobs whose periods
 * match the current tick. Returns the number of jobs successfully released.
 */
uint32_t check_periodic_releases(Scheduler *sc);

/*
 * Executes a selected job for one tick, limited by the scheduler's
 * available UT capacity, and updates its remaining workload.
 */
bool execute_job_tick(Scheduler *sc, int job_index);

/*
 * Selects the next active job according to the configured RMS or EDF
 * scheduling algorithm. Returns -1 if no job can be selected.
 */
int scheduler_select_job(Scheduler *sc);

/*
 * Processes one scheduler tick by releasing periodic jobs, checking
 * deadline misses, selecting a job, and executing its workload.
 * Returns the selected task ID through the output parameter.
 */
bool scheduler_run_tick(Scheduler *sc, uint32_t *selected_task_id);

/*
 * Checks active jobs for missed deadlines, updates the corresponding
 * statistics, and aborts jobs that have not completed on time.
 */
void check_deadline_misses(Scheduler *sc);

/*
 * Registers an event for the sporadic task and creates a job if the
 * minimum inter-arrival interval has elapsed. Rejects events that
 * arrive too early or cannot be assigned a job slot.
 */
bool scheduler_register_sporadic_event(Scheduler *sc);

#endif