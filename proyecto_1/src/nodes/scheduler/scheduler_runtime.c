#include "scheduler_runtime.h"

#include "task_conversion.h"

void scheduler_runtime_init(SchedulerRuntime *runtime)
{
    if (runtime != NULL)
        *runtime = (SchedulerRuntime){0};
}

bool scheduler_runtime_start(
    SchedulerRuntime *runtime,
    const TaskStore *store,
    uint32_t ut_per_tick,
    SchedulerType type,
    RejectReason *reason
)
{
    if (reason != NULL)
        *reason = REJECT_INVALID_STATE;

    if (runtime == NULL || store == NULL)
        return false;

    if (runtime->running) {
        if (reason != NULL)
            *reason = REJECT_NONE;
        return true;
    }

    if (runtime->prepared) {
        runtime->running = true;
        if (reason != NULL)
            *reason = REJECT_NONE;
        return true;
    }

    if (store->count == 0u) {
        if (reason != NULL)
            *reason = REJECT_NO_TASKS;
        return false;
    }

    if (store->count > TASK_MAX_PER_NODE ||
        ut_per_tick == 0u || (type != RMS && type != EDF)) {
        return false;
    }

    init(&runtime->scheduler, ut_per_tick, type);

    for (size_t i = 0u; i < store->count; ++i) {
        SchedulerTask task;
        if (!scheduler_task_from_stored(&store->tasks[i], &task) ||
            !add_task(&task, &runtime->scheduler)) {
            if (reason != NULL)
                *reason = REJECT_INVALID_TASK;
            return false;
        }
    }

    runtime->prepared = true;
    runtime->running = true;
    if (reason != NULL)
        *reason = REJECT_NONE;
    return true;
}

void scheduler_runtime_stop(SchedulerRuntime *runtime)
{
    if (runtime != NULL)
        runtime->running = false;
}

bool scheduler_runtime_run_tick(
    SchedulerRuntime *runtime,
    uint32_t *selected_task_id
)
{
    if (runtime == NULL || selected_task_id == NULL)
        return false;

    if (!runtime->running) {
        *selected_task_id = UINT32_MAX;
        return true;
    }

    return scheduler_run_tick(&runtime->scheduler, selected_task_id);
}

bool scheduler_runtime_trigger_sporadic(
    SchedulerRuntime *runtime,
    uint32_t task_id,
    RejectReason *reason
)
{
    if (reason != NULL)
        *reason = REJECT_INVALID_STATE;

    if (runtime == NULL || !runtime->running || !runtime->prepared)
        return false;

    Scheduler *scheduler = &runtime->scheduler;
    uint32_t task_index = scheduler->sporadic_task_index;
    if (task_index == UINT32_MAX ||
        task_index >= scheduler->task_count ||
        scheduler->tasks[task_index].id != task_id) {
        if (reason != NULL)
            *reason = REJECT_INVALID_TASK;
        return false;
    }

    if (!scheduler_register_sporadic_event(scheduler)) {
        if (reason != NULL)
            *reason = REJECT_INSUFFICIENT_CAPACITY;
        return false;
    }

    if (reason != NULL)
        *reason = REJECT_NONE;
    return true;
}
