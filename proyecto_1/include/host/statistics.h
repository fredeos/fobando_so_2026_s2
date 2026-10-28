#ifndef HOST_STATISTICS_H
#define HOST_STATISTICS_H

#include <stdbool.h>
#include <stddef.h>

#include "common/events.h"

/* Estadísticas por pareja nodo/tarea, para conservar las migraciones */
typedef struct {
    uint32_t node_id;
    uint32_t task_id;
    Task task;
    bool definition_known;
    bool active;
    bool cost_known;
    uint32_t cost_ticks;
    uint64_t accepted;
    uint64_t rejected;
    uint64_t releases;
    uint64_t slices;
    uint64_t execution_ticks;
    uint64_t completed;
    uint64_t deadline_misses;
    uint64_t ignored;
    uint64_t switches;
    uint64_t node_failures;
    uint64_t response_count;
    uint64_t latency_count;
    long double response_sum;
    long double latency_sum;
} TaskStatistics;

/* Utilización medida sobre el tiempo activo observado, no sobre tiempo de pared */
typedef struct {
    uint32_t node_id;
    uint64_t first_tick;
    uint64_t last_tick;
    uint64_t observed_ticks;
    uint64_t busy_ticks;
    uint64_t idle_ticks;
    uint64_t failures;
    uint64_t switches;
    uint64_t bad_frames;
    uint64_t overlapping_slices;
    uint64_t out_of_window_slices;
    bool missing_cost;
    bool implicit_start;
    long double theoretical_mean;
    long double theoretical_peak;
    long double theoretical_final;

    /* Estado temporal utilizado durante la reconstrucción ordenada */
    bool running;
    bool explicit_start;
    bool seen_runtime;
    uint64_t clock;
    uint64_t busy_end;
    uint64_t idle_end;
    uint64_t slice_end;
    long double load;
    long double theory_area;
} NodeStatistics;

typedef struct {
    TaskStatistics *tasks;
    size_t task_count;
    NodeStatistics *nodes;
    size_t node_count;
} ReportStatistics;

/* Reconstruir por tick; no presupone que los distintos productores lleguen ordenados */
bool statistics_build(const Event *events, size_t count, ReportStatistics *out);
void statistics_free(ReportStatistics *statistics);

/* Nombres constantes, utilizados tanto en HTML como en CSV */
const char *report_event_name(EventType type);
const char *report_reject_reason(int32_t reason);

#endif // HOST_STATISTICS_H
