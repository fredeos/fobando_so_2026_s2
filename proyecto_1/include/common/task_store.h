#ifndef COMMON_TASK_STORE_H
#define COMMON_TASK_STORE_H

#include <stddef.h>
#include <stdint.h>

#include "common/events.h"

/* Definición recibida y costo en ticks calculado por el planificador del Host. */
typedef struct {
    Task task;
    uint32_t cost_ticks;
} StoredTask;

/* Almacenamiento temporal por MCU; se vacía al reiniciar el dispositivo. */
typedef struct {
    StoredTask tasks[TASK_MAX_PER_NODE];
    size_t count;
} TaskStore;

/* Inicializa la tabla vacía antes de comenzar a recibir mensajes. */
void task_store_init(TaskStore *store);

/*
 * Valida y copia una tarea desplegada a la tabla.
 * Devuelve REJECT_NONE si se almacena o el motivo que debe enviarse en NACK.
 */
RejectReason task_store_add(
    TaskStore *store,
    const Task *task,
    uint32_t cost_ticks
);

#endif
