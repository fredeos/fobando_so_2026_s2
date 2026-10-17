#ifndef RMS_H
#define RMS_H

#include "task.h"
#include "scheduler.h"


int rms_select_job(Scheduler *sc);

// Execute the selected Job during one tick
bool execute_job_tick(Scheduler *sc, int job_index);

#endif