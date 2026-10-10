#ifndef HOST_TASK_PARSER_H
#define HOST_TASK_PARSER_H

#include <stdbool.h>
#include <stddef.h>

#include "common/task.h"

/*
 * Conjunto de tareas cargadas.
 */
typedef struct {
    Task *items;
    size_t count;
} TaskList;

/*
 * Lee un archivo CSV.
 *
 * out debe estar inicializado en cero
 * o haberse liberado previamente.
 */
bool task_parser_load(
    const char *path,
    TaskList *out,
    char *error,
    size_t error_size
);

/* Libera las tareas cargadas */
void task_list_free(
    TaskList *list
);

#endif