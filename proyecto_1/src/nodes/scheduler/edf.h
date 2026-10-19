#ifndef EDF_H
#define EDF_H

#include "task.h"
#include "scheduler.h"

int edf_select_job(Scheduler *sc);

#endif