#ifndef HOST_ADMISSION_H
#define HOST_ADMISSION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common/events.h"
#include "host/node.h"

/* Algoritmos de calendarización */
typedef enum {
    SCHED_RMS = 1,
    SCHED_EDF = 2
} SchedulingAlgorithm;

/*
 * Resultado de una prueba de admisión.
 */
typedef struct {
    bool accepted;

    RejectReason reason;

    uint32_t cost_ticks;

    double utilization_after;
    double utilization_limit;
} AdmissionResult;

/* Comprueba los atributos de una tarea */
bool task_is_valid(
    const Task *task
);

/* Calcula el costo de una tarea en un nodo */
uint32_t calculate_cost(
    const Task *task,
    const Node *node
);

/* Calcula la cota de utilización de RMS */
double rms_bound(
    size_t task_count
);

/* Realiza la prueba de admisión RMS o EDF */
AdmissionResult admission_check(
    const Node *node,
    const Task *task,
    SchedulingAlgorithm algorithm
);

#endif