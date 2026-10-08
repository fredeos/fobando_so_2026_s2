#ifndef COMMON_TASK_H
#define COMMON_TASK_H

#include <stdint.h>

/*
 * Tipos de tareas soportados.
 * Los valores coinciden con el formato del CSV.
 */
typedef enum {
    TASK_PERIODIC = 'P',
    TASK_SPORADIC = 'S'
} TaskType;

/*
 * Definición de una tarea.
 * No contiene información de su ejecución.
 */
typedef struct {
    uint32_t id;          // Identificador único
    TaskType type;        // Periódica o esporádica
    uint32_t work;        // Trabajo requerido en UT
    uint32_t period;      // Período o intervalo mínimo en ticks
    uint32_t deadline;    // Plazo relativo en ticks
    int32_t criticality;  // Mayor valor = mayor criticidad
} Task;

#endif