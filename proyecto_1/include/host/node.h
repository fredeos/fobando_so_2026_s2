#ifndef HOST_NODE_H
#define HOST_NODE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common/task.h"

/* Capacidad máxima de tareas por nodo */
#define NODE_MAX_TASKS 32u

/* Estados posibles de un nodo */
typedef enum {
    NODE_DISCONNECTED = 0,
    NODE_CALIBRATING,
    NODE_READY,
    NODE_RUNNING,
    NODE_FAILED
} NodeStatus;

/*
 * Tarea asignada a un nodo.
 * Almacena su definición y costo calculado.
 */
typedef struct {
    Task task;
    uint32_t cost_ticks;
} AssignedTask;

/*
 * Representación de un nodo en el Host.
 */
typedef struct {
    uint32_t id;
    uint32_t ut_per_tick;

    NodeStatus status;

    AssignedTask tasks[NODE_MAX_TASKS];

    size_t task_count;
    size_t sporadic_count;

    double utilization;
} Node;

/* Inicializa un nodo con una capacidad conocida */
bool node_init(
    Node *node,
    uint32_t id,
    uint32_t ut_per_tick
);

/* Comprueba si una tarea pertenece al nodo */
bool node_has_task(
    const Node *node,
    uint32_t task_id
);

/* Incorpora una tarea previamente admitida */
bool node_add_task(
    Node *node,
    const Task *task,
    uint32_t cost_ticks
);

#endif