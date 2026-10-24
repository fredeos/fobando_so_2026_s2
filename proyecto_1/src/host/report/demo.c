#include "host/report_session.h"

#include <stdio.h>
#include <time.h>

/* Estado privado de cada nodo simulado, utilizado solo dentro de su proceso */
typedef struct {
    uint32_t id;
    uint64_t tick;
    uint32_t previous_task;
    bool failed;
} DemoNode;

static int publish(EventQueue *queue, const Event *event)
{
    EventQueueResult result;
    do {
        result = event_queue_push(queue, event, 200);
    } while (result == EVENT_QUEUE_TIMEOUT);
    return result == EVENT_QUEUE_OK ? 0 : 1;
}

/* Ejemplo de productor: solo publica Event, sin acceder al proceso de reportes */
static int demo_run(EventQueue *queue, void *context)
{
    DemoNode *node = context;
    Event event = {.node_id = node->id, .source = EVENT_SOURCE_HOST};
    event.type = EVENT_NODE_CALIBRATED;
    event.ut_per_tick = 100u;
    if (publish(queue, &event) != 0) return 1;

    for (uint32_t task = 1u; task <= 2u; ++task) {
        event = (Event){.type = EVENT_TASK_ACCEPTED, .source = EVENT_SOURCE_HOST,
            .node_id = node->id, .task_id = node->id * 10u + task,
            .cost_ticks = task == 1u ? 2u : 1u,
            .task = {.id = node->id * 10u + task, .type = TASK_PERIODIC,
                .work = task == 1u ? 200u : 100u, .period = 10u,
                .deadline = task == 1u ? 8u : 5u, .criticality = 1}};
        if (publish(queue, &event) != 0) return 1;
    }
    event = (Event){.type = EVENT_TASK_REJECTED, .source = EVENT_SOURCE_HOST,
        .node_id = node->id, .task_id = 90u + node->id,
        .detail = REJECT_INSUFFICIENT_CAPACITY};
    if (publish(queue, &event) != 0) return 1;
    event = (Event){.type = EVENT_NODE_STARTED, .source = EVENT_SOURCE_NODE, .node_id = node->id};
    if (publish(queue, &event) != 0) return 1;

    while (!event_queue_stop_requested(queue)) {
        struct timespec pause = {.tv_nsec = 50000000L};
        if (node->failed) { nanosleep(&pause, NULL); continue; }
        uint32_t phase = (uint32_t)(node->tick % 10u);
        uint32_t job = (uint32_t)(node->tick / 10u + 1u);
        if (node->id == 2u && node->tick == 30u) {
            event = (Event){.type = EVENT_NODE_FAILED, .source = EVENT_SOURCE_HOST,
                .node_id = node->id, .tick = node->tick};
            node->failed = true;
            if (publish(queue, &event) != 0) return 1;
            continue;
        }
        if (phase == 0u) {
            for (uint32_t task = 1u; task <= 2u; ++task) {
                event = (Event){.type = EVENT_TASK_RELEASED, .source = EVENT_SOURCE_NODE,
                    .node_id = node->id, .task_id = node->id * 10u + task,
                    .job_id = job, .tick = node->tick};
                if (publish(queue, &event) != 0) return 1;
            }
        }
        bool missed = node->id == 1u && job % 3u == 0u;
        uint32_t task = phase < 2u ? node->id * 10u + 1u :
            phase == 2u && !missed ? node->id * 10u + 2u : 0u;
        if (task != 0u && node->previous_task != 0u && task != node->previous_task) {
            event = (Event){.type = EVENT_TASK_SWITCH, .source = EVENT_SOURCE_NODE,
                .node_id = node->id, .task_id = task, .job_id = job, .tick = node->tick};
            if (publish(queue, &event) != 0) return 1;
        }
        event = (Event){.type = task == 0u ? EVENT_IDLE_SLICE : EVENT_EXECUTION_SLICE,
            .source = EVENT_SOURCE_NODE, .node_id = node->id, .task_id = task,
            .job_id = job, .tick = node->tick, .duration_ticks = 1u};
        if (publish(queue, &event) != 0) return 1;
        if (phase == 1u || (phase == 2u && !missed)) {
            event = (Event){.type = EVENT_TASK_COMPLETED, .source = EVENT_SOURCE_NODE,
                .node_id = node->id, .task_id = task, .job_id = job, .tick = node->tick + 1u};
            if (publish(queue, &event) != 0) return 1;
        }
        if (phase == 5u && missed) {
            event = (Event){.type = EVENT_DEADLINE_MISSED, .source = EVENT_SOURCE_NODE,
                .node_id = node->id, .task_id = node->id * 10u + 2u,
                .job_id = job, .tick = node->tick};
            if (publish(queue, &event) != 0) return 1;
        }
        node->previous_task = task;
        ++node->tick;
        nanosleep(&pause, NULL);
    }
    return 0;
}

/* En un driver real, publicar PARADA_NODO solo después de confirmar STOP */
static int demo_stop(EventQueue *queue, void *context)
{
    DemoNode *node = context;
    Event event = {.type = EVENT_NODE_STOPPED, .source = EVENT_SOURCE_NODE,
        .node_id = node->id, .tick = node->tick};
    return publish(queue, &event);
}

int report_demo_run(const char *directory)
{
    DemoNode nodes[] = {{.id = 1u}, {.id = 2u}};
    ReportWorker workers[] = {
        {.run = demo_run, .stop = demo_stop, .context = &nodes[0]},
        {.run = demo_run, .stop = demo_stop, .context = &nodes[1]}
    };
    printf("Demo de reportes: dos nodos simulados. Presione q para finalizar.\n");
    printf("Tick simulado: 50 ms; falla del nodo 2 en tick 30.\n");
    int result = report_session_run(directory, workers, 2u);
    if (result == 0) printf("Cierre completo. Reporte: %s/report.html\n", directory);
    else fprintf(stderr, "La sesión terminó con errores; revise el reporte si fue generado en %s.\n", directory);
    return result;
}
