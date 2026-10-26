#ifndef COMMON_EVENTS_H
#define COMMON_EVENTS_H

#include <stdint.h>
#include "common/task.h"

/* Origen del evento */
typedef enum {
    EVENT_SOURCE_HOST = 0,
    EVENT_SOURCE_NODE = 1
} EventSource;

/* Eventos que pueden ocurrir durante la ejecución */
typedef enum {
    EVENT_TASK_ACCEPTED = 1,
    EVENT_TASK_REJECTED,
    EVENT_NODE_CALIBRATED,
    EVENT_TASK_RELEASED,
    EVENT_EXECUTION_SLICE,
    EVENT_IDLE_SLICE,
    EVENT_TASK_COMPLETED,
    EVENT_DEADLINE_MISSED,
    EVENT_SPORADIC_IGNORED,
    EVENT_TICK_REPORT,
    EVENT_NODE_FAILED,
    EVENT_NODE_STARTED,
    EVENT_NODE_STOPPED,
    EVENT_SERIAL_BAD_FRAME,
    EVENT_TASK_SWITCH
} EventType;

/* Motivos por los que una tarea puede rechazarse */
typedef enum {
    REJECT_NONE = 0,
    REJECT_INVALID_TASK,
    REJECT_DUPLICATE_TASK,
    REJECT_INSUFFICIENT_CAPACITY,
    REJECT_SPORADIC_LIMIT,
    REJECT_NO_AVAILABLE_NODE,
    REJECT_SPORADIC_NODE_FAILED,
    REJECT_CALIBRATION_FAILED
} RejectReason;

/*
 * Evento utilizado internamente por el Host.
 * Se enviará al proceso de reportes mediante IPC.
 *
 * Los campos que no correspondan al evento
 * deben inicializarse en cero.
 */
typedef struct {
    EventType type;
    EventSource source;

    uint32_t node_id;         // Nodo asociado
    uint32_t task_id;         // Tarea asociada
    uint32_t job_id;          // Instancia de la tarea

    uint64_t tick;            // Tick desde START
    uint64_t host_time_ns;    // Marca temporal del Host

    uint32_t duration_ticks;  // Duración del tramo
    uint32_t cost_ticks;      // Costo calculado de la tarea
    uint32_t ut_per_tick;     // Capacidad calibrada del nodo

    int32_t detail;           // Información adicional
    Task task;                // Definición de la tarea, si aplica
} Event;

#endif