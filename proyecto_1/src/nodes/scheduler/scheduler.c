#include "scheduler.h"
#include "task.h"
#include "ut.h"
#include "rms.h"
#include "edf.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

static void check_deadline_misses(Scheduler *sc);
// ---------------------- init ----------------------

/*
 * Initializes the scheduler, selects the scheduling algorithm, and sets the
 * execution capacity for each tick. It also resets task and job counters,
 * prepares the list of available job slots, and initializes deadline and
 * sporadic task statistics.
 */
void init(Scheduler *sc, uint32_t sn, SchedulerType type){

    if (sc == NULL) {
        return;
    }

    sc->type = type;
    sc->on_selection = NULL;
    sc->task_count = 0;
    sc->job_count = 0;
    sc->current_tick = 0;
    sc->sn = sn;
    sc->free_count = MAX_JOBS;

    sc->deadline_missed = false;
    sc->deadline_miss_count = 0;
    sc->last_missed_task_id = UINT32_MAX;
    sc->last_missed_deadline = UINT32_MAX;

    sc->sporadic_task_index = UINT32_MAX;
    sc->sporadic_last_release_tick = 0;
    sc->sporadic_rejected_count = 0;
    sc->sporadic_completed_count = 0;
    sc->sporadic_has_released = false;

    sc->sporadic_total_response_ticks = 0;
    sc->sporadic_last_response_ticks = 0;
    sc->sporadic_min_response_ticks = UINT32_MAX;
    sc->sporadic_max_response_ticks = 0;

    // Initialize all job slots as available for future executions.
    for (uint32_t i = 0; i < MAX_JOBS; i++){
        sc->free_index[i] = i;
        sc->jobs[i].active = false;
    }
}

// ---------------------- add_task ----------------------

/*
 * Registers a periodic or sporadic task after validating its configuration
 * and checking that the scheduler has enough space. Only one sporadic task
 * is allowed per scheduler, so its index is stored for future event releases.
 */
bool add_task(Task *task, Scheduler *sc)
{
    if (sc == NULL || task == NULL) {
        return false;
    }

    if (sc->task_count >= MAX_TASKS) {
        return false;
    }

    if (task->T == 0) {
        return false;
    }

    if (task->type != 'P' && task->type != 'S') {
        return false;
    }

    // Allow at most one sporadic task per scheduler.
    if (task->type == 'S' &&
        sc->sporadic_task_index != UINT32_MAX) {
        return false;
    }

    sc->tasks[sc->task_count] = *task;

    // Save the sporadic task index so external events can identify it.
    if (task->type == 'S') {
        sc->sporadic_task_index = sc->task_count;
    }

    sc->task_count++;

    return true;
}

// ---------------------- get_free_index ----------------------

/*
 * Retrieves an available job slot and removes it from the free-slot list.
 * Returns -1 when all slots are occupied, allowing the caller to handle
 * the failure without accessing an invalid job position.
 */
int get_free_index(Scheduler *sc){
    if (sc->free_count == 0){
        return -1;
    }

    sc->free_count--;

    return sc->free_index[sc->free_count];
}

// ---------------------- free_job ----------------------

/*
 * Deactivates a completed or aborted job and returns its slot to the free
 * list so it can be reused. The function also updates the number of active
 * jobs and rejects invalid indices or slots that are already inactive.
 */
bool free_job(Scheduler *sc, uint32_t index) {

    if (sc == NULL || index >= MAX_JOBS || sc->job_count == 0) {
        return false;
    }

    if (!sc->jobs[index].active) {
        return false;
    }

    sc->jobs[index].active = false;

    // Return the slot to the free list for future job releases.
    sc->free_index[sc->free_count] = index;
    sc->free_count++;
    sc->job_count--;

    return true;
}

// ---------------------- release_job ----------------------

/*
 * Creates a job from a registered task and stores it in an available slot.
 * The job receives its release tick, absolute deadline, remaining workload,
 * and active status so the scheduler can select it for execution.
 */
bool release_job(Scheduler *sc, uint32_t task_index) {

    // Validate the scheduler and the index of the task to release.
    if (sc == NULL || task_index >= sc->task_count) {
        return false;
    }

    Task *task = &sc->tasks[task_index];

    // Reserve an available slot before creating the new job.
    int job_index = get_free_index(sc);

    if (job_index == -1) {
        return false;
    }

    Job *job = &sc->jobs[job_index];
    job->task = task;
    job->release = sc->current_tick;
    job->deadline = sc->current_tick + task->D;
    job->ut_remaining = task->W;
    job->active = true;
    sc->job_count++;

    return true;
}

// ---------------------- check_periodic_releases ----------------------

/*
 * Checks all registered tasks and releases a new job whenever a periodic
 * task's period matches the current tick. Sporadic tasks are skipped because
 * their jobs are released by external events instead of periodic intervals.
 * Returns the number of periodic jobs successfully released.
 */
uint32_t check_periodic_releases(Scheduler *sc) {

    if (sc == NULL) {
        return 0;
    }

    uint32_t released = 0;

    for (uint32_t i = 0; i < sc->task_count; i++) {

        Task *current_task = &sc->tasks[i];

        // Sporadic tasks are released only when an event is registered.
        if (current_task->type != 'P') {
            continue;
        }

        // Release a periodic job whenever its period is reached.
        if (sc->current_tick % current_task->T == 0) {
            if (release_job(sc, i)) {
                released++;
            }
        }
    }

    return released;
}

// ---------------------- execute_uts ----------------------

/*
 * Executes the requested number of work units by repeatedly calling the
 * workload function. The amount is limited by the caller to respect the
 * scheduler's execution capacity for the current tick.
 */
static void execute_uts(uint32_t amount) {

    for (uint32_t i = 0; i < amount; i++) {
        unidad_trabajo();
    }
}

// ---------------------- execute_job_tick ----------------------

/*
 * Executes part of a selected job according to the available work units
 * per tick. Updates the remaining workload and, when a sporadic job finishes,
 * records its response time before releasing the job's storage slot.
 */
bool execute_job_tick(Scheduler *sc, int job_index) {

    // Validate the scheduler, job index, and execution calibration.
    if (sc == NULL) {
        return false;
    }

    if (job_index < 0 || job_index >= MAX_JOBS) {
        return false;
    }

    if (sc->sn == 0) {
        return false;
    }

    Job *job = &sc->jobs[job_index];

    // Only active jobs with a valid task can be executed.
    if (!job->active || job->task == NULL) {
        return false;
    }

    // Execute no more work units than the current tick allows.
    uint32_t uts = job->ut_remaining;

    if (uts > sc->sn) {
        uts = sc->sn;
    }

    execute_uts(uts);

    // Update the remaining workload after executing the selected work units.
    job->ut_remaining -= uts;

    if (job->ut_remaining == 0) {

        if (job->task->type == 'S') {

            // Include the release and completion ticks in the response time.
            uint32_t response_ticks =
                (sc->current_tick - job->release) + 1;

            // Update the number of completions and response-time statistics.
            sc->sporadic_completed_count++;
            sc->sporadic_total_response_ticks += response_ticks;
            sc->sporadic_last_response_ticks = response_ticks;

            if (response_ticks < sc->sporadic_min_response_ticks) {
                sc->sporadic_min_response_ticks = response_ticks;
            }

            if (response_ticks > sc->sporadic_max_response_ticks) {
                sc->sporadic_max_response_ticks = response_ticks;
            }
        }

        // Reuse the job slot after the workload has been completed.
        free_job(sc, (uint32_t)job_index);
    }

    return true;
}

// ---------------------- scheduler_select_job ----------------------

/*
 * Selects the next active job using the scheduling algorithm configured
 * during initialization. RMS selects according to fixed task priorities,
 * while EDF selects according to absolute deadlines. Returns -1 if the
 * scheduler is invalid or no supported algorithm is configured.
 */
int scheduler_select_job(Scheduler *sc)
{
    if (sc == NULL) {
        return -1;
    }

    switch (sc->type) {
        case RMS:
            return rms_select_job(sc);

        case EDF:
            return edf_select_job(sc);

        default:
            return -1;
    }
}

// ---------------------- scheduler_run_tick ----------------------

/*
 * Executes one scheduler tick by releasing periodic jobs, checking deadline
 * misses, selecting the next job, and executing its allowed workload.
 * Returns the selected task ID through the output parameter, or UINT32_MAX
 * when no job is selected. The tick advances after processing is complete.
 */
bool scheduler_run_tick(Scheduler *sc, uint32_t *selected_task_id)
{
    // Validate the scheduler, execution capacity, and output parameter.
    if (sc == NULL || sc->sn == 0 || selected_task_id == NULL) {
        return false;
    }

    // Use UINT32_MAX to indicate that no task has been selected yet.
    *selected_task_id = UINT32_MAX;

    // Release periodic jobs that become ready at the current tick.
    check_periodic_releases(sc);

    // Abort jobs whose deadlines have been reached without completion.
    check_deadline_misses(sc);

    // Select a job according to RMS or EDF.
    int job_index = scheduler_select_job(sc);

    if (job_index >= 0) {
        Job *job = &sc->jobs[job_index];

        if (job->task == NULL) {
            return false;
        }

        *selected_task_id = job->task->id;
        if (sc->on_selection != NULL) sc->on_selection(*selected_task_id);

        if (!execute_job_tick(sc, job_index)) {
            return false;
        }
    }

    if (job_index < 0 && sc->on_selection != NULL)
        sc->on_selection(UINT32_MAX);

    // Advance the simulation after processing the current tick.
    sc->current_tick++;

    return true;
}

// ---------------------- check_deadline_misses ----------------------

/*
 * Checks all active jobs for deadline violations. When a job reaches its
 * deadline with work remaining, the scheduler records the missed deadline
 * and aborts the job so it cannot be selected again. Other active jobs
 * remain available for subsequent scheduling ticks.
 */
static void check_deadline_misses(Scheduler *sc)
{
    if (sc == NULL) {
        return;
    }

    for (uint32_t i = 0; i < MAX_JOBS; i++) {

        Job *job = &sc->jobs[i];

        // Ignore inactive jobs and jobs without a valid task.
        if (!job->active || job->task == NULL) {
            continue;
        }

        // A deadline is not considered missed before its deadline tick.
        if (sc->current_tick < job->deadline) {
            continue;
        }

        // Release a completed job if it is still marked as active.
        if (job->ut_remaining == 0) {
            free_job(sc, i);
            continue;
        }

        // Store information about the latest missed deadline.
        sc->deadline_missed = true;
        sc->deadline_miss_count++;
        sc->last_missed_task_id = job->task->id;
        sc->last_missed_deadline = job->deadline;

        // Abort the job and return its slot to the free list.
        free_job(sc, i);
    }
}

// ---------------------- scheduler_register_sporadic_event ----------------------

/*
 * Registers an event for the scheduler's sporadic task and releases a new
 * job if the minimum inter-arrival interval has elapsed. Events that arrive
 * too early are rejected and counted. If no sporadic task is registered or
 * no job slot is available, the function returns false.
 */
bool scheduler_register_sporadic_event(Scheduler *sc)
{
    if (sc == NULL) {
        return false;
    }

    // A sporadic task must be registered before its events can be accepted.
    if (sc->sporadic_task_index == UINT32_MAX) {
        return false;
    }

    uint32_t task_index = sc->sporadic_task_index;
    Task *task = &sc->tasks[task_index];

    // Reject events that occur before the task's minimum inter-arrival time.
    if (sc->sporadic_has_released) {
        uint32_t elapsed_ticks =
            sc->current_tick - sc->sporadic_last_release_tick;

        if (elapsed_ticks < task->T) {
            sc->sporadic_rejected_count++;
            return false;
        }
    }

    // Create a job for the event; update release information only on success.
    if (!release_job(sc, task_index)) {
        return false;
    }

    sc->sporadic_last_release_tick = sc->current_tick;
    sc->sporadic_has_released = true;

    return true;
}