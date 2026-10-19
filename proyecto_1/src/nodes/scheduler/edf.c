#include "edf.h"
#include "task.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

int edf_select_job(Scheduler *sc) {

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

        // Calculate absolute deadlines
        uint32_t current_deadline = current->deadline;
        uint32_t best_deadline = best->deadline;

        // EDF: earliest deadline has highest priority
        if (current_deadline < best_deadline) {
            selected = (int)i;
        }

        // Tie: same deadline
        else if (current_deadline == best_deadline) {

            // Oldest release first
            if (current->release < best->release) {
                selected = (int)i;
            }

            // Same release: lowest task ID
            else if (current->release == best->release &&
                     current->task->id < best->task->id) {
                selected = (int)i;
            }
        }
    }

    return selected;
}


// Executes one scheduling tick.
bool scheduler_run_tick(Scheduler *sc)
{
    if (sc == NULL || sc->sn == 0) {
        return false;
    }

    // Release periodic jobs at the current tick.
    check_periodic_releases(sc);

    // Select the highest-priority active job.
    int job_index = scheduler_select_job(sc);

    // Execute one scheduling interval.
    if (job_index >= 0) {
        if (!execute_job_tick(sc, job_index)) {
            return false;
        }
    }

    // Advance the scheduling clock.
    sc->current_tick++;

    return true;
}