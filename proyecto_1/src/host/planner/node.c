#include "host/node.h"

#include <string.h>

/*
 * Inicializa un nodo con su identificador
 * y capacidad de procesamiento.
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
 * Comprueba si una tarea ya está
 * asignada al nodo.
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
 * Agrega una tarea previamente admitida
 * y actualiza la utilización del nodo.
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

/*
 * Elimina una tarea del nodo.
 *
 * Mantiene el orden de las tareas restantes
 * y recalcula la utilización y el número
 * de tareas esporádicas.
 */
bool node_remove_task(
    Node *node,
    uint32_t task_id
)
{
    if (node == NULL)
        return false;

    /* Buscar la tarea */
    size_t index = 0u;

    while (index < node->task_count &&
           node->tasks[index].task.id != task_id) {

        ++index;
    }

    /* La tarea no existe */
    if (index == node->task_count)
        return false;

    /* Desplazar las tareas restantes */
    for (size_t i = index + 1u;
         i < node->task_count;
         ++i) {

        node->tasks[i - 1u] = node->tasks[i];
    }

    --node->task_count;

    /* Limpiar la última posición */
    node->tasks[node->task_count] = (AssignedTask){0};

    /* Recalcular estadísticas de asignación */
    node->sporadic_count = 0u;
    node->utilization = 0.0;

    for (size_t i = 0u; i < node->task_count; ++i) {

        const AssignedTask *assigned = &node->tasks[i];

        if (assigned->task.type == TASK_SPORADIC)
            ++node->sporadic_count;

        node->utilization +=
            (double)assigned->cost_ticks /
            (double)assigned->task.period;
    }

    return true;
}