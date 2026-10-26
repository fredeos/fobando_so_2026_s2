#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "common/task_store.h"
#include "scheduler_runtime.h"

static void test_start_pause_and_resume(void)
{
    TaskStore store;
    task_store_init(&store);

    const Task periodic = {
        .id = 11u, .type = TASK_PERIODIC, .work = 6u,
        .period = 2u, .deadline = 2u, .criticality = 1
    };
    assert(task_store_add(&store, &periodic, 2u) == REJECT_NONE);

    SchedulerRuntime runtime;
    scheduler_runtime_init(&runtime);
    RejectReason reason = REJECT_INVALID_STATE;
    assert(scheduler_runtime_start(&runtime, &store, 4u, RMS, &reason));
    assert(reason == REJECT_NONE);
    assert(runtime.running && runtime.prepared);
    assert(runtime.scheduler.task_count == 1u);

    uint32_t selected_task_id = UINT32_MAX;
    assert(scheduler_runtime_run_tick(&runtime, &selected_task_id));
    assert(selected_task_id == periodic.id);
    assert(runtime.scheduler.current_tick == 1u);
    assert(runtime.scheduler.job_count == 1u);
    bool pending_job_found = false;
    for (size_t i = 0u; i < MAX_JOBS; ++i) {
        if (runtime.scheduler.jobs[i].active) {
            assert(runtime.scheduler.jobs[i].ut_remaining == 2u);
            pending_job_found = true;
        }
    }
    assert(pending_job_found);

    scheduler_runtime_stop(&runtime);
    assert(!runtime.running && runtime.prepared);
    assert(scheduler_runtime_run_tick(&runtime, &selected_task_id));
    assert(selected_task_id == UINT32_MAX);
    assert(runtime.scheduler.current_tick == 1u);

    assert(scheduler_runtime_start(&runtime, &store, 4u, RMS, &reason));
    assert(runtime.running);
    assert(runtime.scheduler.current_tick == 1u);
    assert(scheduler_runtime_run_tick(&runtime, &selected_task_id));
    assert(runtime.scheduler.current_tick == 2u);
    assert(runtime.scheduler.job_count == 0u);
}

static void test_sporadic_messages_require_running_matching_task(void)
{
    TaskStore store;
    task_store_init(&store);

    const Task sporadic = {
        .id = 22u, .type = TASK_SPORADIC, .work = 3u,
        .period = 5u, .deadline = 5u, .criticality = 1
    };
    assert(task_store_add(&store, &sporadic, 1u) == REJECT_NONE);

    SchedulerRuntime runtime;
    scheduler_runtime_init(&runtime);
    RejectReason reason = REJECT_NONE;
    assert(scheduler_runtime_start(&runtime, &store, 2u, EDF, &reason));
    assert(scheduler_runtime_trigger_sporadic(
        &runtime, sporadic.id, &reason));
    assert(runtime.scheduler.job_count == 1u);

    assert(!scheduler_runtime_trigger_sporadic(
        &runtime, sporadic.id + 1u, &reason));
    assert(reason == REJECT_INVALID_TASK);
    assert(!scheduler_runtime_trigger_sporadic(
        &runtime, sporadic.id, &reason));
    assert(reason == REJECT_INSUFFICIENT_CAPACITY);

    scheduler_runtime_stop(&runtime);
    assert(!scheduler_runtime_trigger_sporadic(
        &runtime, sporadic.id, &reason));
    assert(reason == REJECT_INVALID_STATE);
}

static void test_start_rejects_empty_store(void)
{
    TaskStore store;
    task_store_init(&store);

    SchedulerRuntime runtime;
    scheduler_runtime_init(&runtime);
    RejectReason reason = REJECT_NONE;
    assert(!scheduler_runtime_start(&runtime, &store, 4u, RMS, &reason));
    assert(reason == REJECT_NO_TASKS);
    assert(!runtime.prepared && !runtime.running);
}

int main(void)
{
    test_start_pause_and_resume();
    test_sporadic_messages_require_running_matching_task();
    test_start_rejects_empty_store();
    puts("OK: scheduler runtime tests");
    return 0;
}
