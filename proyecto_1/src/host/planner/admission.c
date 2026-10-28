#include "host/admission.h"

#include <math.h>

/*
 * Verifica que los atributos de una tarea sean válidos.
 */
bool task_is_valid(const Task *task)
{
    return task != NULL &&
           (task->type == TASK_PERIODIC ||
            task->type == TASK_SPORADIC) &&
           task->work > 0u &&
           task->period > 0u &&
           task->deadline > 0u;
}

/*
 * Calcula el costo de una tarea en un nodo.
 *
 * C(i,n) = ceil(W(i) / s(n))
 *
 * Se utiliza aritmética entera para evitar
 * problemas de redondeo.
 */
uint32_t calculate_cost(
    const Task *task,
    const Node *node
)
{
    if (!task_is_valid(task) ||
        node == NULL ||
        node->ut_per_tick == 0u) {

        return 0u;
    }

    return 1u +
        (task->work - 1u) / node->ut_per_tick;
}

/*
 * Calcula la cota de utilización de RMS.
 *
 * U <= n * (2^(1/n) - 1)
 */
double rms_bound(size_t task_count)
{
    if (task_count == 0u)
        return 0.0;

    return (double)task_count *
        expm1(log(2.0) / (double)task_count);
}

/*
 * Comprueba si un nodo puede admitir una tarea.
 *
 * EDF: U <= 1
 * RMS: U <= n(2^(1/n) - 1)
 *
 * Las tareas esporádicas se consideran periódicas
 * utilizando su intervalo mínimo entre llegadas.
 */
AdmissionResult admission_check(
    const Node *node,
    const Task *task,
    SchedulingAlgorithm algorithm
)
{
    AdmissionResult result = {
        .reason = REJECT_INVALID_TASK
    };

    // Validar tarea y algoritmo
    if (!task_is_valid(task) ||
        (algorithm != SCHED_RMS &&
         algorithm != SCHED_EDF)) {

        return result;
    }

    // Verificar disponibilidad del nodo
    if (node == NULL ||
        node->ut_per_tick == 0u ||
        (node->status != NODE_READY &&
         node->status != NODE_RUNNING)) {

        result.reason = REJECT_NO_AVAILABLE_NODE;
        return result;
    }

    // Evitar tareas duplicadas
    if (node_has_task(node, task->id)) {

        result.reason = REJECT_DUPLICATE_TASK;
        return result;
    }

    // Máximo una tarea esporádica por nodo
    if (task->type == TASK_SPORADIC &&
        node->sporadic_count >= 1u) {

        result.reason = REJECT_SPORADIC_LIMIT;
        return result;
    }

    // Verificar espacio disponible
    if (node->task_count >= NODE_MAX_TASKS) {

        result.reason = REJECT_INSUFFICIENT_CAPACITY;
        return result;
    }

    // Calcular costo de ejecución
    result.cost_ticks = calculate_cost(task, node);

    // Calcular utilización después de agregar la tarea
    result.utilization_after =
        node->utilization +
        (double)result.cost_ticks / (double)task->period;

    // Determinar límite según el algoritmo
    result.utilization_limit =
        algorithm == SCHED_EDF
        ? 1.0
        : rms_bound(node->task_count + 1u);

    // Comprobar admisión
    result.accepted =
        result.utilization_after <=
        result.utilization_limit + 1e-12;

    // Establecer resultado
    result.reason = result.accepted
        ? REJECT_NONE
        : REJECT_INSUFFICIENT_CAPACITY;

    return result;
}