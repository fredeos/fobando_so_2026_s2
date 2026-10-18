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


