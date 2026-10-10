#ifndef HOST_MANUAL_TASKS_H
#define HOST_MANUAL_TASKS_H

#include <stdbool.h>
#include <stddef.h>

#include "host/planner.h"

/*
 * Estados de una solicitud manual.
 *
 * Representan su estado en el Host,
 * no el estado de ejecución en el nodo.
 */
typedef enum {
    MANUAL_EMPTY = 0,
    MANUAL_REJECTED,
    MANUAL_PENDING,
    MANUAL_CONFIRMED,
    MANUAL_CANCELLED
} ManualTaskStatus;

/*
 * Solicitud de incorporación manual.
 *
 * Conserva la tarea, la decisión del
 * planificador y el estado de la solicitud.
 */
typedef struct {
    Task task;

    PlannerDecision decision;

    ManualTaskStatus status;
} ManualTaskRequest;

/*
 * Convierte una línea ingresada por el
 * usuario en una estructura Task.
 *
 * Formato: id,P|S,W,T,D,criticidad
 */
bool manual_task_parse(
    const char *line,
    Task *out,
    char *error,
    size_t error_size
);

/*
 * Intenta admitir una tarea y reserva
 * capacidad en el nodo seleccionado.
 *
 * Una admisión exitosa queda pendiente
 * hasta confirmar el despliegue.
 */
bool manual_task_submit(
    Planner *planner,
    const Task *task,
    ManualTaskRequest *request
);

/*
 * Confirma una solicitud pendiente.
 * Debe utilizarse únicamente tras un ACK
 * satisfactorio del nodo correspondiente.
 */
bool manual_task_confirm(
    ManualTaskRequest *request
);

/*
 * Cancela una solicitud pendiente.
 * Elimina la reserva del planificador.
 */
bool manual_task_cancel(
    Planner *planner,
    ManualTaskRequest *request
);

#endif