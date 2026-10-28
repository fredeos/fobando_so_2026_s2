#include <assert.h>
#include <stdio.h>

#include "common/task_store.h"

int main(void)
{
    TaskStore store;
    task_store_init(&store);

    Task task = {
        .id = 7u,
        .type = TASK_PERIODIC,
        .work = 120u,
        .period = 30u,
        .deadline = 25u,
        .criticality = 4
    };

    assert(task_store_add(&store, &task, 3u) == REJECT_NONE);
    assert(store.count == 1u);
    assert(store.tasks[0].task.id == task.id);
    assert(store.tasks[0].task.type == task.type);
    assert(store.tasks[0].task.work == task.work);
    assert(store.tasks[0].task.period == task.period);
    assert(store.tasks[0].task.deadline == task.deadline);
    assert(store.tasks[0].task.criticality == task.criticality);
    assert(store.tasks[0].cost_ticks == 3u);
    assert(task_store_add(&store, &task, 3u) == REJECT_DUPLICATE_TASK);

    Task invalid = task;
    invalid.work = 0u;
    assert(task_store_add(&store, &invalid, 3u) == REJECT_INVALID_TASK);
    assert(task_store_add(&store, &task, 0u) == REJECT_INVALID_TASK);

    task_store_init(&store);
    task.type = TASK_SPORADIC;
    assert(task_store_add(&store, &task, 3u) == REJECT_NONE);
    task.id++;
    assert(task_store_add(&store, &task, 3u) == REJECT_SPORADIC_LIMIT);

    task_store_init(&store);
    task.type = TASK_PERIODIC;
    for (uint32_t i = 0u; i < TASK_MAX_PER_NODE; ++i) {
        task.id = i;
        assert(task_store_add(&store, &task, 1u) == REJECT_NONE);
    }
    task.id = TASK_MAX_PER_NODE;
    assert(task_store_add(&store, &task, 1u) == REJECT_INSUFFICIENT_CAPACITY);
    assert(store.count == TASK_MAX_PER_NODE);

    puts("OK: task store tests");
    return 0;
}
