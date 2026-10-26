#include "host/planner.h"

#include <stdlib.h>

/*
 * Inicializa el planificador y valida
 * el algoritmo y la heurística seleccionados.
 */
bool planner_init(
    Planner *planner,
    SchedulingAlgorithm algorithm,
    PlacementHeuristic heuristic
)
{
    if (planner == NULL ||
        (algorithm != SCHED_RMS &&
         algorithm != SCHED_EDF) ||
        (heuristic != PLACEMENT_FIRST_FIT &&
         heuristic != PLACEMENT_WORST_FIT)) {

        return false;
    }

    *planner = (Planner){0};

    planner->algorithm = algorithm;
    planner->heuristic = heuristic;

    return true;
}

/*
 * Registra un nodo con su capacidad calibrada.
 */
bool planner_add_node(
    Planner *planner,
    uint32_t id,
    uint32_t ut_per_tick
)
{
    if (planner == NULL ||
        planner->node_count >= PLANNER_MAX_NODES) {

        return false;
    }

    // Verificar que el identificador no exista
    for (size_t i = 0; i < planner->node_count; ++i) {

        if (planner->nodes[i].id == id)
            return false;
    }

    if (!node_init(
            &planner->nodes[planner->node_count],
            id,
            ut_per_tick)) {

        return false;
    }

    ++planner->node_count;

    return true;
}

/*
 * Comprueba si una tarea ya fue asignada
 * a cualquiera de los nodos.
 */
static bool planner_has_task(
    const Planner *planner,
    uint32_t id
)
{
    for (size_t i = 0; i < planner->node_count; ++i) {

        if (node_has_task(&planner->nodes[i], id))
            return true;
    }

    return false;
}

/*
 * Ordena los índices de los nodos por:
 *
 * 1. Mayor capacidad UT/tick.
 * 2. Menor ID en caso de empate.
 */
static void node_order(
    const Planner *planner,
    size_t order[PLANNER_MAX_NODES]
)
{
    for (size_t i = 0; i < planner->node_count; ++i)
        order[i] = i;

    // Insertion sort
    for (size_t i = 1; i < planner->node_count; ++i) {

        size_t item = order[i];
        size_t j = i;

        while (j > 0) {

            const Node *a =
                &planner->nodes[order[j - 1u]];

            const Node *b =
                &planner->nodes[item];

            if (a->ut_per_tick > b->ut_per_tick ||
                (a->ut_per_tick == b->ut_per_tick &&
                 a->id < b->id)) {

                break;
            }

            order[j] = order[j - 1u];
            --j;
        }

        order[j] = item;
    }
}

/*
 * Asigna una tarea utilizando First-Fit
 * o Worst-Fit.
 */
PlannerDecision planner_assign_task(
    Planner *planner,
    const Task *task
)
{
    PlannerDecision decision = {
        .reason = REJECT_INVALID_TASK
    };

    if (planner == NULL || !task_is_valid(task))
        return decision;

    decision.task_id = task->id;

    // Evitar asignaciones duplicadas
    if (planner_has_task(planner, task->id)) {

        decision.reason = REJECT_DUPLICATE_TASK;
        return decision;
    }

    // Ordenar nodos por capacidad
    size_t order[PLANNER_MAX_NODES];

    node_order(planner, order);

    size_t best = PLANNER_MAX_NODES;

    AdmissionResult best_result = {0};

    double best_free = -1.0;

    bool available = false;

    bool sporadic_only =
        task->type == TASK_SPORADIC;

    // Evaluar nodos disponibles
    for (size_t k = 0; k < planner->node_count; ++k) {

        size_t idx = order[k];

        AdmissionResult attempt = admission_check(
            &planner->nodes[idx],
            task,
            planner->algorithm
        );

        if (attempt.reason != REJECT_NO_AVAILABLE_NODE) {

            available = true;

            if (attempt.reason != REJECT_SPORADIC_LIMIT)
                sporadic_only = false;
        }

        if (!attempt.accepted)
            continue;

        // Capacidad libre posterior a la asignación
        double free_after =
            1.0 - attempt.utilization_after;

        if (best == PLANNER_MAX_NODES ||
            (planner->heuristic == PLACEMENT_WORST_FIT &&
             free_after > best_free + 1e-12)) {

            best = idx;

            best_result = attempt;

            best_free = free_after;
        }

        // First-Fit selecciona el primer nodo válido
        if (planner->heuristic == PLACEMENT_FIRST_FIT)
            break;
    }

    // Ningún nodo pudo aceptar la tarea
    if (best == PLANNER_MAX_NODES) {

        decision.reason =
            !available
            ? REJECT_NO_AVAILABLE_NODE
            : sporadic_only
              ? REJECT_SPORADIC_LIMIT
              : REJECT_INSUFFICIENT_CAPACITY;

        return decision;
    }

    // Incorporar la tarea al nodo elegido
    if (!node_add_task(
            &planner->nodes[best],
            task,
            best_result.cost_ticks)) {

        return decision;
    }

    decision.accepted = true;
    decision.reason = REJECT_NONE;

    decision.node_id = planner->nodes[best].id;

    decision.cost_ticks = best_result.cost_ticks;

    decision.utilization_after =
        best_result.utilization_after;

    return decision;
}

bool planner_remove_task(
    Planner *planner,
    uint32_t node_id,
    uint32_t task_id
)
{
    if (planner == NULL)
        return false;

    /* El node_id viene de la decisión tomada al asignar la tarea. */
    for (size_t i = 0u; i < planner->node_count; ++i) {
        if (planner->nodes[i].id == node_id)
            return node_remove_task(&planner->nodes[i], task_id);
    }
    return false;
}

/*
 * Información utilizada para ordenar
 * las tareas antes de su asignación.
 */
typedef struct {
    size_t index;

    double reference_utilization;

    int32_t criticality;

    uint32_t task_id;
} SortEntry;

/*
 * Comparador:
 *
 * 1. Mayor utilización.
 * 2. Mayor criticidad.
 * 3. Menor identificador.
 * 4. Posición original.
 */
static int compare_tasks(
    const void *lhs,
    const void *rhs
)
{
    const SortEntry *a = lhs;
    const SortEntry *b = rhs;

    if (a->reference_utilization >
        b->reference_utilization)
        return -1;

    if (a->reference_utilization <
        b->reference_utilization)
        return 1;

    if (a->criticality > b->criticality)
        return -1;

    if (a->criticality < b->criticality)
        return 1;

    if (a->task_id < b->task_id)
        return -1;

    if (a->task_id > b->task_id)
        return 1;

    return (a->index > b->index) -
           (a->index < b->index);
}

/*
 * Planifica la carga inicial.
 *
 * Primero ordena las tareas por utilización
 * en el nodo más rápido.
 *
 * Después aplica First-Fit o Worst-Fit.
 */
bool planner_plan_batch(
    Planner *planner,
    const Task *tasks,
    size_t count,
    PlannerDecision *decisions
)
{
    if (planner == NULL ||
        (count && (tasks == NULL || decisions == NULL))) {

        return false;
    }

    if (count == 0u)
        return true;

    // Evitar desbordamiento en la reserva
    if (count > SIZE_MAX / sizeof(SortEntry))
        return false;

    SortEntry *order =
        malloc(count * sizeof(*order));

    if (order == NULL)
        return false;

    // Identificar el nodo más rápido
    const Node *fastest = NULL;

    for (size_t i = 0; i < planner->node_count; ++i) {

        if (fastest == NULL ||
            planner->nodes[i].ut_per_tick >
            fastest->ut_per_tick) {

            fastest = &planner->nodes[i];
        }
    }

    // Calcular utilización de referencia
    for (size_t i = 0; i < count; ++i) {

        order[i] = (SortEntry){

            .index = i,

            .reference_utilization =
                (fastest && task_is_valid(&tasks[i]))
                ? (double)calculate_cost(
                    &tasks[i], fastest
                  ) / tasks[i].period
                : -1.0,

            .criticality = tasks[i].criticality,

            .task_id = tasks[i].id
        };
    }

    // Ordenar tareas
    qsort(
        order,
        count,
        sizeof(*order),
        compare_tasks
    );

    // Asignar tareas en el orden calculado
    for (size_t i = 0; i < count; ++i) {

        size_t original = order[i].index;

        bool repeated = false;

        // Detectar IDs duplicados en el archivo
        for (size_t j = 0; j < original; ++j) {

            if (tasks[j].id == tasks[original].id) {

                repeated = true;
                break;
            }
        }

        decisions[original] = repeated
            ? (PlannerDecision){
                .task_id = tasks[original].id,
                .reason = REJECT_DUPLICATE_TASK
              }
            : planner_assign_task(
                planner,
                &tasks[original]
              );
    }

    free(order);

    return true;
}