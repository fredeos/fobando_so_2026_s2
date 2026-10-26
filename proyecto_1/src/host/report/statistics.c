#include "host/statistics.h"

#include <stdlib.h>
#include <string.h>

/* Historial por instancia, independiente de las tareas actualmente asignadas */
typedef struct {
    uint32_t node_id, task_id, job_id;
    bool occupied, released, executed, completed;
    uint64_t release_tick;
} JobHistory;

typedef struct {
    JobHistory *items;
    size_t count;
    size_t capacity;
} JobTable;

/* Reservar una entrada nueva sin perder el bloque original si realloc falla */
static void *append_entry(void **items, size_t *count, size_t size)
{
    if (*count >= SIZE_MAX / size)
        return NULL;
    void *next = realloc(*items, (*count + 1u) * size);
    if (next == NULL)
        return NULL;
    *items = next;
    void *entry = (char *)next + *count * size;
    memset(entry, 0, size);
    ++*count;
    return entry;
}

static NodeStatistics *get_node(ReportStatistics *statistics, uint32_t id)
{
    for (size_t i = 0; i < statistics->node_count; ++i)
        if (statistics->nodes[i].node_id == id) return &statistics->nodes[i];
    void *items = statistics->nodes;
    NodeStatistics *node = append_entry(&items,
                                        &statistics->node_count, sizeof(*node));
    statistics->nodes = items;
    if (node != NULL) node->node_id = id;
    return node;
}

static TaskStatistics *get_task(ReportStatistics *statistics, const Event *event)
{
    for (size_t i = 0; i < statistics->task_count; ++i) {
        TaskStatistics *task = &statistics->tasks[i];
        if (task->node_id == event->node_id && task->task_id == event->task_id)
            return task;
    }
    void *items = statistics->tasks;
    TaskStatistics *task = append_entry(&items,
                                        &statistics->task_count, sizeof(*task));
    statistics->tasks = items;
    if (task != NULL) {
        task->node_id = event->node_id;
        task->task_id = event->task_id;
    }
    return task;
}

/* Índice por instancia para evitar búsquedas cuadráticas en ejecuciones largas */
static size_t job_index(uint32_t node, uint32_t task, uint32_t job, size_t capacity)
{
    uint64_t hash = ((uint64_t)node << 32) ^ task;
    hash ^= (uint64_t)job * UINT64_C(0x9e3779b97f4a7c15);
    hash ^= hash >> 30;
    hash *= UINT64_C(0xbf58476d1ce4e5b9);
    hash ^= hash >> 27;
    return (size_t)hash & (capacity - 1u);
}

static bool grow_jobs(JobTable *table)
{
    size_t capacity = table->capacity == 0u ? 128u : table->capacity * 2u;
    if (capacity < table->capacity || capacity > SIZE_MAX / sizeof(JobHistory)) return false;
    JobHistory *items = calloc(capacity, sizeof(*items));
    if (items == NULL) return false;
    for (size_t i = 0; i < table->capacity; ++i) {
        JobHistory *job = &table->items[i];
        if (!job->occupied) continue;
        size_t slot = job_index(job->node_id, job->task_id, job->job_id, capacity);
        while (items[slot].occupied) slot = (slot + 1u) & (capacity - 1u);
        items[slot] = *job;
    }
    free(table->items);
    table->items = items;
    table->capacity = capacity;
    return true;
}

static JobHistory *get_job(JobTable *table, const Event *event)
{
    if (table->count >= table->capacity / 2u && !grow_jobs(table)) return NULL;
    size_t slot = job_index(event->node_id, event->task_id, event->job_id, table->capacity);
    while (table->items[slot].occupied) {
        JobHistory *job = &table->items[slot];
        if (job->node_id == event->node_id && job->task_id == event->task_id &&
            job->job_id == event->job_id) return job;
        slot = (slot + 1u) & (table->capacity - 1u);
    }
    JobHistory *job = &table->items[slot];
    job->occupied = true;
    job->node_id = event->node_id;
    job->task_id = event->task_id;
    job->job_id = event->job_id;
    ++table->count;
    return job;
}

/* Mantener el orden de recepción para eventos del mismo nodo y tick */
static int compare_events(const void *left, const void *right)
{
    const Event *a = *(const Event *const *)left;
    const Event *b = *(const Event *const *)right;
    if (a->node_id != b->node_id) return a->node_id < b->node_id ? -1 : 1;
    if (a->tick != b->tick) return a->tick < b->tick ? -1 : 1;
    return a == b ? 0 : (a < b ? -1 : 1);
}

static bool task_event(EventType type)
{
    return type == EVENT_TASK_ACCEPTED || type == EVENT_TASK_REJECTED ||
           type == EVENT_TASK_RELEASED || type == EVENT_EXECUTION_SLICE ||
           type == EVENT_TASK_COMPLETED || type == EVENT_DEADLINE_MISSED ||
           type == EVENT_SPORADIC_IGNORED || type == EVENT_TASK_SWITCH;
}

static bool runtime_event(EventType type)
{
    return type != EVENT_TASK_ACCEPTED && type != EVENT_TASK_REJECTED &&
           type != EVENT_NODE_CALIBRATED && type != EVENT_SERIAL_BAD_FRAME;
}

/* Recalcular C/T después de una asignación o una falla */
static void update_load(ReportStatistics *statistics, NodeStatistics *node)
{
    node->load = 0.0L;
    for (size_t i = 0; i < statistics->task_count; ++i) {
        TaskStatistics *task = &statistics->tasks[i];
        if (task->node_id != node->node_id || !task->active) continue;
        if (!task->definition_known || !task->cost_known) {
            node->missing_cost = true;
            continue;
        }
        node->load += (long double)task->cost_ticks / task->task.period;
    }
    if (node->load > node->theoretical_peak)
        node->theoretical_peak = node->load;
    node->theoretical_final = node->load;
}

/* Avanzar la integral de carga únicamente mientras el nodo está activo */
static void advance(NodeStatistics *node, uint64_t tick)
{
    if (node->running) {
        uint64_t duration = tick - node->clock;
        node->observed_ticks += duration;
        node->theory_area += node->load * duration;
    }
    node->clock = tick;
}

/* Contar la unión de intervalos para no duplicar tiempo ocupado */
static uint64_t union_duration(uint64_t start, uint64_t end, uint64_t *previous_end)
{
    uint64_t begin = start > *previous_end ? start : *previous_end;
    uint64_t duration = end > begin ? end - begin : 0u;
    if (end > *previous_end) *previous_end = end;
    return duration;
}

/* Acumular contadores y tiempos de respuesta de una instancia conocida */
static bool update_task(TaskStatistics *task, const Event *event,
                         JobTable *jobs)
{
    switch (event->type) {
        case EVENT_TASK_ACCEPTED:
            ++task->accepted;
            task->active = true;
            task->task = event->task;
            task->definition_known = event->task.id == event->task_id &&
                (event->task.type == TASK_PERIODIC || event->task.type == TASK_SPORADIC) &&
                event->task.work > 0u && event->task.period > 0u && event->task.deadline > 0u;
            task->cost_ticks = event->cost_ticks;
            task->cost_known = event->cost_ticks > 0u;
            break;
        case EVENT_TASK_REJECTED: ++task->rejected; break;
        case EVENT_TASK_RELEASED: ++task->releases; break;
        case EVENT_EXECUTION_SLICE:
            ++task->slices;
            if (UINT64_MAX - task->execution_ticks < event->duration_ticks) return false;
            task->execution_ticks += event->duration_ticks;
            break;
        case EVENT_TASK_COMPLETED: ++task->completed; break;
        case EVENT_DEADLINE_MISSED: ++task->deadline_misses; break;
        case EVENT_SPORADIC_IGNORED: ++task->ignored; break;
        case EVENT_TASK_SWITCH: ++task->switches; break;
        default: break;
    }

    if (event->type != EVENT_TASK_RELEASED && event->type != EVENT_EXECUTION_SLICE &&
        event->type != EVENT_TASK_COMPLETED) return true;

    JobHistory *job = get_job(jobs, event);
    if (job == NULL) return false;
    if (event->type == EVENT_TASK_RELEASED && !job->released) {
        job->released = true;
        job->release_tick = event->tick;
    } else if (event->type == EVENT_EXECUTION_SLICE && !job->executed) {
        job->executed = true;
        if (job->released) {
            task->latency_sum += event->tick - job->release_tick;
            ++task->latency_count;
        }
    } else if (event->type == EVENT_TASK_COMPLETED && !job->completed) {
        job->completed = true;
        if (job->released) {
            task->response_sum += event->tick - job->release_tick;
            ++task->response_count;
        }
    }
    return true;
}

bool statistics_build(const Event *events, size_t count, ReportStatistics *out)
{
    if (out == NULL || (count > 0u && events == NULL) ||
        count > SIZE_MAX / sizeof(const Event *)) return false;

    ReportStatistics statistics = {0};
    JobTable jobs = {0};
    const Event **ordered = count == 0u ? NULL : malloc(count * sizeof(*ordered));
    if (count > 0u && ordered == NULL) return false;

    for (size_t i = 0; i < count; ++i) {
        if (events[i].type < EVENT_TASK_ACCEPTED || events[i].type > EVENT_TASK_SWITCH ||
            UINT64_MAX - events[i].tick < events[i].duration_ticks) goto fail;
        ordered[i] = &events[i];
        NodeStatistics *node = get_node(&statistics, events[i].node_id);
        if (node == NULL) goto fail;
        if (events[i].type == EVENT_NODE_STARTED) node->explicit_start = true;
    }
    if (count > 0u) qsort(ordered, count, sizeof(*ordered), compare_events);

    for (size_t i = 0; i < count; ++i) {
        const Event *event = ordered[i];
        NodeStatistics *node = get_node(&statistics, event->node_id);
        if (node == NULL) goto fail;
        advance(node, event->tick);

        if (runtime_event(event->type)) {
            if (!node->seen_runtime) {
                node->seen_runtime = true;
                node->first_tick = event->tick;
                if (!node->explicit_start) {
                    node->running = true;
                    node->implicit_start = true;
                }
            }
            uint64_t end = event->tick;
            if (event->type == EVENT_EXECUTION_SLICE || event->type == EVENT_IDLE_SLICE)
                end += event->duration_ticks;
            if (end > node->last_tick) node->last_tick = end;
        }

        if (task_event(event->type)) {
            TaskStatistics *task = get_task(&statistics, event);
            if (task == NULL || !update_task(task, event, &jobs)) goto fail;
            if (event->type != EVENT_TASK_ACCEPTED && event->type != EVENT_TASK_REJECTED &&
                (!task->active || !task->definition_known || !task->cost_known))
                node->missing_cost = true;
        }

        switch (event->type) {
            case EVENT_NODE_STARTED: node->running = true; break;
            case EVENT_NODE_STOPPED:
                if (node->slice_end > event->tick) ++node->out_of_window_slices;
                node->running = false;
                break;
            case EVENT_NODE_FAILED:
                if (node->slice_end > event->tick) ++node->out_of_window_slices;
                ++node->failures;
                node->running = false;
                for (size_t j = 0; j < statistics.task_count; ++j) {
                    TaskStatistics *task = &statistics.tasks[j];
                    if (task->node_id == node->node_id && task->active) {
                        ++task->node_failures;
                        task->active = false;
                    }
                }
                update_load(&statistics, node);
                break;
            case EVENT_TASK_ACCEPTED: update_load(&statistics, node); break;
            case EVENT_TASK_SWITCH: ++node->switches; break;
            case EVENT_SERIAL_BAD_FRAME: ++node->bad_frames; break;
            case EVENT_EXECUTION_SLICE:
            case EVENT_IDLE_SLICE: {
                uint64_t end = event->tick + event->duration_ticks;
                if (!node->running) ++node->out_of_window_slices;
                if (event->duration_ticks > 0u && event->tick < node->slice_end)
                    ++node->overlapping_slices;
                if (end > node->slice_end) node->slice_end = end;
                if (event->type == EVENT_EXECUTION_SLICE)
                    node->busy_ticks += union_duration(event->tick, end, &node->busy_end);
                else node->idle_ticks += union_duration(event->tick, end, &node->idle_end);
                break;
            }
            default: break;
        }
    }

    for (size_t i = 0; i < statistics.node_count; ++i) {
        NodeStatistics *node = &statistics.nodes[i];
        if (node->last_tick > node->clock) advance(node, node->last_tick);
        if (node->observed_ticks > 0u)
            node->theoretical_mean = node->theory_area / node->observed_ticks;
    }
    free(ordered);
    free(jobs.items);
    *out = statistics;
    return true;

fail:
    free(ordered);
    free(jobs.items);
    statistics_free(&statistics);
    return false;
}

void statistics_free(ReportStatistics *statistics)
{
    if (statistics == NULL) return;
    free(statistics->tasks);
    free(statistics->nodes);
    memset(statistics, 0, sizeof(*statistics));
}

const char *report_event_name(EventType type)
{
    static const char *const names[] = {
        "DESCONOCIDO", "TAREA_ACEPTADA", "TAREA_RECHAZADA", "NODO_CALIBRADO",
        "LIBERACION", "EJECUCION", "INACTIVIDAD", "FINALIZACION", "PLAZO_INCUMPLIDO",
        "ESPORADICA_IGNORADA", "REPORTE_TICK", "FALLA_NODO", "INICIO_NODO",
        "PARADA_NODO", "TRAMA_INVALIDA", "CAMBIO_TAREA"
    };
    return type >= EVENT_TASK_ACCEPTED && type <= EVENT_TASK_SWITCH
        ? names[type] : names[0];
}

const char *report_reject_reason(int32_t reason)
{
    switch (reason) {
        case REJECT_NONE: return "Sin rechazo";
        case REJECT_INVALID_TASK: return "Tarea inválida";
        case REJECT_DUPLICATE_TASK: return "Tarea duplicada";
        case REJECT_INSUFFICIENT_CAPACITY: return "Capacidad insuficiente";
        case REJECT_SPORADIC_LIMIT: return "Límite de esporádicas";
        case REJECT_NO_AVAILABLE_NODE: return "Sin nodo disponible";
        case REJECT_SPORADIC_NODE_FAILED: return "Falla del nodo de la esporádica";
        case REJECT_CALIBRATION_FAILED: return "Falla de calibración";
        case REJECT_INVALID_STATE: return "Estado de ejecución no válido";
        case REJECT_NO_TASKS: return "El nodo no tiene tareas desplegadas";
        default: return "Motivo desconocido";
    }
}
