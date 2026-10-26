#ifndef TASK_H
#define TASK_H

#include <stdint.h>
#include <stdbool.h>

/*
 * Scheduler-specific task representation.
 * The name SchedulerTask keeps it distinct from common Task, the JSON type.
 */

typedef struct SchedulerTask
{
    uint32_t id;
    char type;
    uint32_t W; // UT quantity
    uint32_t T; // period of time
    uint32_t D; // relative end time
    uint32_t criticality; // how important the task is
    uint32_t C; // tick equivalent
} SchedulerTask;

typedef struct Job
{
    SchedulerTask *task;
    uint32_t release; // a partir de que tick empezò
    uint32_t deadline; 
    uint32_t ut_remaining; // cuantas uts le quedan
    bool active;

} Job;



#endif