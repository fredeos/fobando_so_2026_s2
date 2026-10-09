#include "host/node.h"

#include <string.h>

/*
 * Inicializa un nodo.
 * La capacidad debe provenir de la calibración.
 */
bool node_init(
    Node *node,
    uint32_t id,
    uint32_t ut_per_tick
)
{
    if (node == NULL || ut_per_tick == 0u)
        return false;

    memset(node, 0, sizeof(*node));

    node->id = id;
    node->ut_per_tick = ut_per_tick;
    node->status = NODE_READY;

    return true;
}

/*
 * Comprueba si existe una tarea con el ID indicado.
 */
bool node_has_task(
    const Node *node,
    uint32_t task_id
)
{
    if (node == NULL)
        return false;

    for (size_t i = 0; i < node->task_count; ++i) {

        if (node->tasks[i].task.id == task_id)
            return true;
    }

    return false;
}

/*
 * Agrega una tarea al nodo y actualiza su utilización.
 *
 * Debe llamarse únicamente después de que
 * admission_check() haya aprobado la tarea.
 */
bool node_add_task(
    Node *node,
    const Task *task,
    uint32_t cost_ticks
)
{
    if (node == NULL ||
        task == NULL ||
        cost_ticks == 0u ||
        task->period == 0u ||
        node->task_count >= NODE_MAX_TASKS ||
        node_has_task(node, task->id) ||
        (task->type == TASK_SPORADIC &&
         node->sporadic_count != 0u)) {

        return false;
    }

    node->tasks[node->task_count++] = (AssignedTask){
        .task = *task,
        .cost_ticks = cost_ticks
    };

    if (task->type == TASK_SPORADIC)
        ++node->sporadic_count;

    node->utilization +=
        (double)cost_ticks / (double)task->period;

    return true;
}