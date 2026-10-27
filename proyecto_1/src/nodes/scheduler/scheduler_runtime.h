#ifndef SCHEDULER_RUNTIME_H
#define SCHEDULER_RUNTIME_H

#include <stdbool.h>
#include <stdint.h>

#include "common/task_store.h"
#include "scheduler.h"

typedef struct {
    Scheduler scheduler;
    bool prepared;
    bool running;
} SchedulerRuntime;

void scheduler_runtime_init(SchedulerRuntime *runtime);

/*
 * Prepares the scheduler from stored tasks on first START. Later START
 * requests resume the existing scheduler without resetting its tick or jobs.
 */
bool scheduler_runtime_start(
    SchedulerRuntime *runtime,
    const TaskStore *store,
    uint32_t ut_per_tick,
    SchedulerType type,
    RejectReason *reason
);

/* Stores a task and appends it to an already prepared scheduler if needed. */
RejectReason scheduler_runtime_add_task(
    SchedulerRuntime *runtime,
    TaskStore *store,
    const Task *task,
    uint32_t cost_ticks
);

/* Pauses execution without discarding tasks, jobs, or scheduler time. */
void scheduler_runtime_stop(SchedulerRuntime *runtime);

/* Runs one scheduler tick only while the runtime is started. */
bool scheduler_runtime_run_tick(
    SchedulerRuntime *runtime,
    uint32_t *selected_task_id
);

/* Releases a job for the registered sporadic task while running. */
bool scheduler_runtime_trigger_sporadic(
    SchedulerRuntime *runtime,
    uint32_t task_id,
    RejectReason *reason
);

#endif
