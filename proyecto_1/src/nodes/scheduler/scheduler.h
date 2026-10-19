#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdint.h>
#include <stdbool.h>
#include "task.h"


#define MAX_TASKS 20
#define MAX_JOBS 40

typedef enum {
    RMS,
    EDF
} SchedulerType;


typedef struct Scheduler {
    SchedulerType type;

    Task tasks[MAX_TASKS]; // tasks assigned by the host
    Job jobs[MAX_JOBS]; // active jobs

    uint32_t free_index[MAX_JOBS];
    uint32_t free_count; // how many free spaces do we have

    uint32_t task_count; // how many tasks do we have
    uint32_t job_count; // how many jobs are ACTIVE

    uint32_t current_tick;
    uint32_t sn; // capacity of uts per tick
} Scheduler;

// funciones

// initialize the scheduler
void init(Scheduler *sc, uint32_t sn, SchedulerType type);

// register a task
bool add_task(Task *task, Scheduler *sc);

// returns a free position for a Job
int get_free_index(Scheduler *sc);

// Release a Job position
bool free_job(Scheduler *sc, uint32_t index);

// crear un job a partir de un task
bool release_job(Scheduler *sc, uint32_t task_index);

uint32_t check_periodic_releases(Scheduler *sc);


// Execute the selected Job during one tick
bool execute_job_tick(Scheduler *sc, int job_index);

int scheduler_select_job(Scheduler *sc);

bool scheduler_run_tick(Scheduler *sc);


#endif