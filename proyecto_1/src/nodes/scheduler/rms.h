#ifndef RMS_H
#define RMS_H

#include "task.h"

#define MAX_TASKS 20
#define MAX_JOBS 40


typedef struct RMS {
    Task tasks[MAX_TASKS]; // tasks assigned by the host
    Job jobs[MAX_JOBS]; // active jobs

    uint32_t free_index[MAX_JOBS];
    uint32_t free_count; // how many spaces do we have

    uint32_t task_count; // how many tasks do we have
    uint32_t job_count; // how many jobs are active

    uint32_t current_tick;
    uint32_t sn; // capacity of uts per tick
} RMS;

// funciones

// initialize the scheduler
void rms_init(RMS *rms, uint32_t sn);

// register a task
bool rms_add_task();

// get a free space on the jobs array
int rms_get_free_job();

// free a position of an ended work
int rms_free_job();



#endif