#include "host/failover.h"

#include <string.h>

/*
 * Compara dos tareas para determinar
 * cuál debe reubicarse primero.
 *
 * Criterios:
 * 1. Mayor criticidad.
 * 2. Mayor utilización en el nodo más rápido.
 * 3. Menor identificador.
 */
static bool before(
    const FailoverEntry *a,
    const FailoverEntry *b,
    const Node *fastest
)
{
    /* Priorizar la mayor criticidad */
    if (a->task.criticality != b->task.criticality) {

        return a->task.criticality >
               b->task.criticality;
    }

    /*
     * Comparar la utilización de referencia
     * cuando las criticidades sean iguales.
     */
    if (fastest != NULL &&
        task_is_valid(&a->task) &&
        task_is_valid(&b->task)) {

        uint64_t left =
            (uint64_t)calculate_cost(
                &a->task,
                fastest
            ) * b->task.period;

        uint64_t right =
            (uint64_t)calculate_cost(
                &b->task,
                fastest
            ) * a->task.period;

        if (left != right)
            return left > right;
    }

    /* Desempate final por identificador */
    return a->task.id < b->task.id;
}

/*
 * Gestiona la caída de un nodo.
 *
 * Conserva sus tareas antes de limpiar
 * las asignaciones y utiliza el planificador
 * existente para intentar reubicarlas.
 */
bool failover_handle_node_failure(
    Planner *planner,
    uint32_t failed_node_id,
    FailoverResult *result
)
{
    /* Validar argumentos */
    if (planner == NULL || result == NULL)
        return false;

    Node *failed = NULL;

    const Node *fastest = NULL;

    /*
     * Localizar el nodo fallido y determinar
     * cuál es el nodo sobreviviente más rápido.
     */
    for (size_t i = 0; i < planner->node_count; ++i) {

        Node *node = &planner->nodes[i];

        if (node->id == failed_node_id) {

            failed = node;

        } else if (
            (node->status == NODE_READY ||
             node->status == NODE_RUNNING) &&

            node->ut_per_tick > 0u &&

            (fastest == NULL ||
             node->ut_per_tick > fastest->ut_per_tick ||

             (node->ut_per_tick == fastest->ut_per_tick &&
              node->id < fastest->id))
        ) {

            fastest = node;
        }
    }

    /*
     * Comprobar que el nodo existe,
     * está activo y tiene un estado válido.
     */
    if (failed == NULL ||

        (failed->status != NODE_READY &&
         failed->status != NODE_RUNNING) ||

        failed->task_count > NODE_MAX_TASKS) {

        return false;
    }

    /*
     * Guardar una copia de las tareas
     * antes de modificar el nodo.
     */
    FailoverResult snapshot = {0};

    snapshot.failed_node_id = failed_node_id;

    snapshot.count = failed->task_count;

    /* Índices de las tareas periódicas */
    size_t order[NODE_MAX_TASKS];

    size_t periodic_count = 0u;

    /*
     * Copiar las tareas originales.
     *
     * Las esporádicas se rechazan directamente.
     * Las periódicas se preparan para reubicación.
     */
    for (size_t i = 0; i < snapshot.count; ++i) {

        snapshot.entries[i].task =
            failed->tasks[i].task;

        snapshot.entries[i].previous_cost_ticks =
            failed->tasks[i].cost_ticks;

        snapshot.entries[i].decision.task_id =
            failed->tasks[i].task.id;

        if (failed->tasks[i].task.type ==
            TASK_SPORADIC) {

            snapshot.entries[i].decision.reason =
                REJECT_SPORADIC_NODE_FAILED;

        } else {

            order[periodic_count++] = i;
        }
    }

    /*
     * Marcar el nodo como fallido y eliminar
     * sus asignaciones activas.
     *
     * Las tareas originales se conservan
     * dentro de snapshot.
     */
    failed->status = NODE_FAILED;

    failed->task_count = 0u;

    failed->sporadic_count = 0u;

    failed->utilization = 0.0;

    memset(
        failed->tasks,
        0,
        sizeof(failed->tasks)
    );

    /*
     * Ordenar las tareas periódicas
     * utilizando insertion sort.
     *
     * Se ordenan los índices para conservar
     * el orden original de los resultados.
     */
    for (size_t i = 1; i < periodic_count; ++i) {

        size_t current = order[i];

        size_t j = i;

        while (
            j > 0u &&

            before(
                &snapshot.entries[current],
                &snapshot.entries[order[j - 1u]],
                fastest
            )
        ) {

            order[j] = order[j - 1u];

            --j;
        }

        order[j] = current;
    }

    /*
     * Intentar reubicar las tareas periódicas.
     *
     * planner_assign_task() aplica automáticamente
     * la heurística y admisión configuradas.
     */
    for (size_t i = 0; i < periodic_count; ++i) {

        FailoverEntry *entry =
            &snapshot.entries[order[i]];

        entry->decision = planner_assign_task(
            planner,
            &entry->task
        );
    }

    /* Entregar los resultados */
    *result = snapshot;

    return true;
}