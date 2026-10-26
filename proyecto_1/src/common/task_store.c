#include "common/task_store.h"

#include <string.h>

void task_store_init(TaskStore *store)
{
    if (store != NULL)
        memset(store, 0, sizeof(*store));
}

/*
 * Este chequeo protege la tabla local del nodo; la admisión de capacidad
 * y el cálculo del costo corresponden al Planner del Host.
 */
RejectReason task_store_add(
    TaskStore *store,
    const Task *task,
    uint32_t cost_ticks
)
{
    if (store == NULL || task == NULL ||
        (task->type != TASK_PERIODIC && task->type != TASK_SPORADIC) ||
        task->work == 0u || task->period == 0u || task->deadline == 0u ||
        cost_ticks == 0u) {
        return REJECT_INVALID_TASK;
    }

    if (store->count > TASK_MAX_PER_NODE)
        return REJECT_INSUFFICIENT_CAPACITY;

    for (size_t i = 0u; i < store->count; ++i) {
        if (store->tasks[i].task.id == task->id)
            return REJECT_DUPLICATE_TASK;
        if (task->type == TASK_SPORADIC &&
            store->tasks[i].task.type == TASK_SPORADIC)
            return REJECT_SPORADIC_LIMIT;
    }

    if (store->count == TASK_MAX_PER_NODE)
        return REJECT_INSUFFICIENT_CAPACITY;

    /* Copiar la estructura evita retener referencias al buffer JSON recibido. */
    store->tasks[store->count++] = (StoredTask){
        .task = *task,
        .cost_ticks = cost_ticks
    };
    return REJECT_NONE;
}
