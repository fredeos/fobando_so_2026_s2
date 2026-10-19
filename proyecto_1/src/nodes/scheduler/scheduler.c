#include "scheduler.h"
#include "task.h"
#include "ut.h"
#include "rms.h"
#include "edf.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

// rms initializer
void init(Scheduler *sc, uint32_t sn, SchedulerType type){

    if (sc == NULL) {
        return;
    }

    sc->type = type;
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

    for (uint32_t i = 0; i < MAX_JOBS; i++){
        sc->free_index[i] = i;
        sc->jobs[i].active = false;
    }
}

// ---------------------- add_task ----------------------

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

    if (task->type == 'S') {
        sc->sporadic_task_index = sc->task_count;
    }

    sc->task_count++;

    return true;
}

int get_free_index(Scheduler *sc){
    if (sc->free_count == 0){
        return -1;
    }

    sc->free_count--;

    return sc->free_index[sc->free_count];
}

bool free_job(Scheduler *sc, uint32_t index) {

    if (sc == NULL || index >= MAX_JOBS || sc->job_count == 0) {
        return false;
    }

    if (!sc->jobs[index].active) {
        return false;
    }

    sc->jobs[index].active = false;
    sc->free_index[sc->free_count] = index;
    sc->free_count++;
    sc->job_count--;

    return true;
}

bool release_job(Scheduler *sc, uint32_t task_index) {

    // Validate scheduler and task index
    if (sc == NULL || task_index >= sc->task_count) {
        return false;
    }

    // Get original task
    Task *task = &sc->tasks[task_index];

    // Get available job position
    int job_index = get_free_index(sc);

    if (job_index == -1) {
        return false;
    }

    // Initialize new job
    Job *job = &sc->jobs[job_index];
    job->task = task;
    job->release = sc->current_tick;
    job->deadline = sc->current_tick + task->D;
    job->ut_remaining = task->W;
    job->active = true;
    sc->job_count++;

    return true;
}

uint32_t check_periodic_releases(Scheduler *sc) {

    if (sc == NULL) {
        return 0;
    }

    uint32_t released = 0;

    for (uint32_t i = 0; i < sc->task_count; i++) {

        Task *current_task = &sc->tasks[i];

        if (current_task->type != 'P') {
            continue;
        }

        if (sc->current_tick % current_task->T == 0) {

            if (release_job(sc, i)) {
                released++;
            }
        }
    }

    return released;
}

static void execute_uts(uint32_t amount) {

    for (uint32_t i = 0; i < amount; i++) {
        unidad_trabajo();
    }
}

bool execute_job_tick(Scheduler *sc, int job_index) {

    // Validate scheduler
    if (sc == NULL) {
        return false;
    }

    // Validate index
    if (job_index < 0 || job_index >= MAX_JOBS) {
        return false;
    }

    // Validate calibration
    if (sc->sn == 0) {
        return false;
    }

    Job *job = &sc->jobs[job_index];

    // Validate Job
    if (!job->active || job->task == NULL) {
        return false;
    }

    // Determine how many UTs can be executed
    uint32_t uts = job->ut_remaining;

    if (uts > sc->sn) {
        uts = sc->sn;
    }

    // Execute real workload
    execute_uts(uts);

    job->ut_remaining -= uts;

    if (job->ut_remaining == 0) {
        if (job->task->type == 'S') {
            uint32_t response_ticks =
                (sc->current_tick - job->release) + 1;

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

        free_job(sc, (uint32_t)job_index);
    }

    return true;
}

// Selects a job using the configured scheduling algorithm.
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

// Executes one scheduling tick and reports the selected task ID.
bool scheduler_run_tick(Scheduler *sc, uint32_t *selected_task_id)
{
    if (sc == NULL || sc->sn == 0 || selected_task_id == NULL) {
        return false;
    }

    *selected_task_id = UINT32_MAX;

    // Release periodic jobs for the current tick.
    check_periodic_releases(sc);

    // Detect and abort jobs that have missed their deadlines.
    check_deadline_misses(sc);


    // Select the highest-priority remaining job.
    int job_index = scheduler_select_job(sc);

    if (job_index >= 0) {
        Job *job = &sc->jobs[job_index];

        if (job->task == NULL) {
            return false;
        }

        *selected_task_id = job->task->id;

        if (!execute_job_tick(sc, job_index)) {
            return false;
        }
    }

    sc->current_tick++;

    return true;
}

// Detects, records, and aborts jobs that have missed their deadlines.
static void check_deadline_misses(Scheduler *sc)
{
    if (sc == NULL) {
        return;
    }

    for (uint32_t i = 0; i < MAX_JOBS; i++) {

        Job *job = &sc->jobs[i];

        // Ignore inactive jobs and invalid task references.
        if (!job->active || job->task == NULL) {
            continue;
        }

        // The job has not missed its deadline yet.
        if (sc->current_tick < job->deadline) {
            continue;
        }

        // A job with no remaining work has completed successfully.
        if (job->ut_remaining == 0) {
            free_job(sc, i);
            continue;
        }

        // Record the deadline miss.
        sc->deadline_missed = true;
        sc->deadline_miss_count++;
        sc->last_missed_task_id = job->task->id;
        sc->last_missed_deadline = job->deadline;

        // Abort the job so it cannot be selected again.
        free_job(sc, i);
    }
}

// ---------------------- scheduler_register_sporadic_event ----------------------

bool scheduler_register_sporadic_event(Scheduler *sc)
{
    if (sc == NULL) {
        return false;
    }

    // A sporadic task must be registered first.
    if (sc->sporadic_task_index == UINT32_MAX) {
        return false;
    }

    uint32_t task_index = sc->sporadic_task_index;
    Task *task = &sc->tasks[task_index];

    // Reject events that arrive before the minimum inter-arrival interval.
    if (sc->sporadic_has_released) {
        uint32_t elapsed_ticks = sc->current_tick - sc->sporadic_last_release_tick;

        if (elapsed_ticks < task->T) {
            sc->sporadic_rejected_count++;
            return false;
        }
    }

    // Release a job for the accepted event.
    if (!release_job(sc, task_index)) {
        return false;
    }

    sc->sporadic_last_release_tick = sc->current_tick;
    sc->sporadic_has_released = true;

    return true;
}