#ifndef HOST_PLANNER_H
#define HOST_PLANNER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "host/admission.h"

/* Cantidad máxima de nodos administrados */
#define PLANNER_MAX_NODES 8u

/* Heurísticas de asignación */
typedef enum {
    PLACEMENT_FIRST_FIT = 1,
    PLACEMENT_WORST_FIT = 2
} PlacementHeuristic;

/*
 * Estado del planificador.
 */
typedef struct {
    Node nodes[PLANNER_MAX_NODES];

    size_t node_count;

    SchedulingAlgorithm algorithm;
    PlacementHeuristic heuristic;
} Planner;

/*
 * Resultado de asignar una tarea.
 */
typedef struct {
    bool accepted;

    uint32_t task_id;
    uint32_t node_id;

    uint32_t cost_ticks;

    double utilization_after;

    RejectReason reason;
} PlannerDecision;

/* Inicializa el planificador */
bool planner_init(
    Planner *planner,
    SchedulingAlgorithm algorithm,
    PlacementHeuristic heuristic
);

/* Agrega un nodo calibrado */
bool planner_add_node(
    Planner *planner,
    uint32_t id,
    uint32_t ut_per_tick
);

/* Intenta asignar una tarea individual */
PlannerDecision planner_assign_task(
    Planner *planner,
    const Task *task
);

/*
 * Planifica un conjunto de tareas.
 *
 * Las ordena por utilización decreciente
 * en el nodo más rápido.
 *
 * Las decisiones se devuelven en el mismo
 * orden del arreglo original.
 */
bool planner_plan_batch(
    Planner *planner,
    const Task *tasks,
    size_t count,
    PlannerDecision *decisions
);

#endif