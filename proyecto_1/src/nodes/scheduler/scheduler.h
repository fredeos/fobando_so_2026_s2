#ifndef SCHEDULER_H
#define SCHEDULER_H
#include <stdint.h>
#include <stdbool.h>
#include "common/task.h"
#include "task.h"
#define MAX_TASKS TASK_MAX_PER_NODE
#define MAX_JOBS 40

typedef enum { RMS, EDF } SchedulerType;

typedef struct Scheduler {
    SchedulerType type;
    /* Llamado tras seleccionar el trabajo y antes de sus UT; UINT32_MAX=idle.
     * Debe ser breve y no bloquear, por ejemplo para actualizar los LEDs. */
    void (*on_selection)(uint32_t task_id);
    void (*on_task_released)(uint32_t task_id, uint32_t job_id, uint32_t tick);
    void (*on_task_completed)(uint32_t task_id, uint32_t job_id, uint32_t tick);
    void (*on_deadline_miss)(uint32_t task_id, uint32_t job_id, uint32_t tick);
    SchedulerTask tasks[MAX_TASKS];
    uint32_t task_release_origin[MAX_TASKS];
    Job jobs[MAX_JOBS];
    uint32_t free_index[MAX_JOBS];
    uint32_t free_count;
    uint32_t task_count;
    uint32_t job_count;
    uint32_t current_tick;
    uint32_t last_selected_job_id;
    uint32_t sn;
    bool deadline_missed;
    uint32_t deadline_miss_count;
    uint32_t last_missed_task_id;
    uint32_t last_missed_deadline;
    uint32_t sporadic_task_index;
    uint32_t sporadic_last_release_tick;
    uint32_t sporadic_rejected_count;
    uint32_t sporadic_completed_count;
    uint64_t sporadic_total_response_ticks;
    bool sporadic_has_released;
    uint32_t sporadic_last_response_ticks;
    uint32_t sporadic_min_response_ticks;
    uint32_t sporadic_max_response_ticks;
} Scheduler;

void init(Scheduler *sc, uint32_t sn, SchedulerType type);
bool add_task(SchedulerTask *task, Scheduler *sc);
int get_free_index(Scheduler *sc);
bool free_job(Scheduler *sc, uint32_t index);
bool release_job(Scheduler *sc, uint32_t task_index);
uint32_t check_periodic_releases(Scheduler *sc);
bool execute_job_tick(Scheduler *sc, int job_index);
int scheduler_select_job(Scheduler *sc);
bool scheduler_run_tick(Scheduler *sc, uint32_t *selected_task_id);
bool scheduler_register_sporadic_event(Scheduler *sc);
#endif