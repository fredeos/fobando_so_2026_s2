#include "rms.h"
#include "task.h"
#include <stdbool.h>
#include <stddef.h>

int rms_select_job(Scheduler *sc) {

    if (sc == NULL) {
        return -1;
    }

    int selected = -1;

    for (uint32_t i = 0; i < MAX_JOBS; i++) {

        Job *current = &sc->jobs[i];

        // Ignore inactive jobs
        if (!current->active) {
            continue;
        }

        // Ignore invalid task references
        if (current->task == NULL) {
            continue;
        }

        // First active job
        if (selected == -1) {
            selected = (int)i;
            continue;
        }

        Job *best = &sc->jobs[selected];

        // RMS: shortest period has highest priority
        if (current->task->T < best->task->T) {
            selected = (int)i;
        }

        // Tie: same period
        else if (current->task->T == best->task->T) {
            // Oldest release first
            if (current->release < best->release) {
                selected = (int)i;
            }
            // Same release: lowest task ID
            else if (current->release == best->release && current->task->id < best->task->id) {
                selected = (int)i;
            }
        }
    }

    return selected;
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

    // Update remaining workload
    job->ut_remaining -= uts;

    // Job finished
    if (job->ut_remaining == 0) {

        free_job(sc, (uint32_t)job_index);
    }

    return true;
}