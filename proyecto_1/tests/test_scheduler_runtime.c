#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "common/task_store.h"
#include "scheduler_runtime.h"

typedef struct {
    uint32_t releases;
    uint32_t completions;
    uint32_t misses;
    uint32_t task_id;
    uint32_t job_id;
    uint32_t tick;
} RuntimeEvents;

static RuntimeEvents runtime_events;

static void capture_release(uint32_t task_id, uint32_t job_id, uint32_t tick)
{
    ++runtime_events.releases;
    runtime_events.task_id = task_id;
    runtime_events.job_id = job_id;
    runtime_events.tick = tick;
}

static void capture_completion(uint32_t task_id, uint32_t job_id, uint32_t tick)
{
    ++runtime_events.completions;
    runtime_events.task_id = task_id;
    runtime_events.job_id = job_id;
    runtime_events.tick = tick;
}

static void capture_miss(uint32_t task_id, uint32_t job_id, uint32_t tick)
{
    ++runtime_events.misses;
    runtime_events.task_id = task_id;
    runtime_events.job_id = job_id;
    runtime_events.tick = tick;
}

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

static void test_runtime_event_callbacks(void)
{
    TaskStore store;
    task_store_init(&store);
    const Task task = {
        .id = 33u, .type = TASK_PERIODIC, .work = 6u,
        .period = 10u, .deadline = 4u, .criticality = 1
    };
    assert(task_store_add(&store, &task, 2u) == REJECT_NONE);

    SchedulerRuntime runtime;
    scheduler_runtime_init(&runtime);
    RejectReason reason = REJECT_INVALID_STATE;
    assert(scheduler_runtime_start(&runtime, &store, 4u, RMS, &reason));
    runtime_events = (RuntimeEvents){0};
    runtime.scheduler.on_task_released = capture_release;
    runtime.scheduler.on_task_completed = capture_completion;
    runtime.scheduler.on_deadline_miss = capture_miss;

    uint32_t selected_task_id = UINT32_MAX;
    assert(scheduler_runtime_run_tick(&runtime, &selected_task_id));
    assert(runtime_events.releases == 1u);
    assert(runtime_events.task_id == task.id && runtime_events.job_id == 0u);
    assert(runtime_events.tick == 0u);
    assert(runtime.scheduler.last_selected_job_id == 0u);

    assert(scheduler_runtime_run_tick(&runtime, &selected_task_id));
    assert(runtime_events.completions == 1u);
    assert(runtime_events.task_id == task.id && runtime_events.job_id == 0u);
    assert(runtime_events.tick == 2u);
    assert(runtime_events.misses == 0u);

    task_store_init(&store);
    const Task overdue_task = {
        .id = 34u, .type = TASK_PERIODIC, .work = 5u,
        .period = 10u, .deadline = 1u, .criticality = 1
    };
    assert(task_store_add(&store, &overdue_task, 1u) == REJECT_NONE);
    scheduler_runtime_init(&runtime);
    assert(scheduler_runtime_start(&runtime, &store, 1u, RMS, &reason));
    runtime_events = (RuntimeEvents){0};
    runtime.scheduler.on_task_released = capture_release;
    runtime.scheduler.on_task_completed = capture_completion;
    runtime.scheduler.on_deadline_miss = capture_miss;
    assert(scheduler_runtime_run_tick(&runtime, &selected_task_id));
    assert(scheduler_runtime_run_tick(&runtime, &selected_task_id));
    assert(runtime_events.misses == 1u);
    assert(runtime_events.task_id == overdue_task.id && runtime_events.job_id == 0u);
    assert(runtime_events.tick == 1u);
}

int main(void)
{
    test_start_pause_and_resume();
    test_sporadic_messages_require_running_matching_task();
    test_start_rejects_empty_store();
    test_runtime_event_callbacks();
    puts("OK: scheduler runtime tests");
    return 0;
}
