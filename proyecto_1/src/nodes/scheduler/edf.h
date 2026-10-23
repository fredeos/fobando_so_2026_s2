#ifndef EDF_H
#define EDF_H

#include "task.h"
#include "scheduler.h"

// ---------------------- edf_select_job ----------------------

/*
 * Selects the active job with the earliest absolute deadline.
 * Returns the index of the selected job, or -1 if no active job is available.
 */
int edf_select_job(Scheduler *sc);

#endif