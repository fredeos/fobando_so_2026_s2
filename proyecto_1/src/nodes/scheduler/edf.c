#include "edf.h"
#include "task.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// ---------------------- edf_select_job ----------------------

/*
 * Searches for the active job with the earliest absolute deadline.
 * If deadlines are equal, prioritizes the oldest release, then the lowest task ID.
 * Returns the selected job index, or -1 if no valid active job is available.
 */
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

        // Ignore jobs without a valid task reference
        if (current->task == NULL) {
            continue;
        }

        // Select the first valid active job
        if (selected == -1) {
            selected = (int)i;
            continue;
        }

        Job *best = &sc->jobs[selected];

        uint32_t current_deadline = current->deadline;
        uint32_t best_deadline = best->deadline;

        // Prioritize the job with the earliest absolute deadline
        if (current_deadline < best_deadline) {
            selected = (int)i;
        }

        // Resolve jobs with the same deadline
        else if (current_deadline == best_deadline) {

            // Prioritize the job released first
            if (current->release < best->release) {
                selected = (int)i;
            }

            // If releases are equal, prioritize the lowest task ID
            else if (current->release == best->release &&
                     current->task->id < best->task->id) {
                selected = (int)i;
            }
        }
    }

    return selected;
}