#ifndef SCHEDULER_TASK_CONVERSION_H
#define SCHEDULER_TASK_CONVERSION_H

#include <stdbool.h>

#include "common/task_store.h"
#include "task.h"

/*
 * Convierte una tarea desplegada al formato que consume el scheduler.
 * No modifica el destino si los datos no se pueden representar válidamente.
 */
bool scheduler_task_from_stored(
    const StoredTask *stored,
    SchedulerTask *out
);

#endif
