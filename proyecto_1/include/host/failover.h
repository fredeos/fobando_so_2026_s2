#ifndef HOST_FAILOVER_H
#define HOST_FAILOVER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "host/planner.h"

/*
 * Resultado de la replanificación de una tarea.
 *
 * Conserva la tarea original, su costo anterior
 * y la decisión tomada por el planificador.
 */
typedef struct {

    Task task;

    uint32_t previous_cost_ticks;

    PlannerDecision decision;

} FailoverEntry;

/*
 * Resultado completo de la caída de un nodo.
 *
 * Permite consultar qué tareas fueron
 * reubicadas o rechazadas y sus motivos.
 */
typedef struct {

    uint32_t failed_node_id;

    size_t count;

    FailoverEntry entries[NODE_MAX_TASKS];

} FailoverResult;

/*
 * Gestiona la caída de un nodo.
 *
 * - Marca el nodo como fallido.
 * - Conserva las tareas de los demás nodos.
 * - Reubica las tareas periódicas por criticidad.
 * - Rechaza las tareas esporádicas.
 *
 * No realiza operaciones de comunicación.
 *
 * Retorna false si la solicitud es inválida,
 * sin modificar el estado del planificador.
 */
bool failover_handle_node_failure(
    Planner *planner,
    uint32_t failed_node_id,
    FailoverResult *result
);

#endif