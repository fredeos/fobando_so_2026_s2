#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "host/report.h"
#include "host/statistics.h"

static Event events[64];
static size_t count;

static void add(EventType type, uint32_t node, uint32_t task, uint32_t job,
                uint64_t tick, uint32_t duration)
{
    assert(count < sizeof(events) / sizeof(events[0]));
    events[count++] = (Event){.type = type, .node_id = node, .task_id = task,
        .job_id = job, .tick = tick, .duration_ticks = duration};
}

static void accept(uint32_t node, uint32_t task, uint64_t tick, uint32_t cost,
                   uint32_t period, uint32_t deadline)
{
    add(EVENT_TASK_ACCEPTED, node, task, 0u, tick, 0u);
    events[count - 1u].task = (Task){.id = task, .type = TASK_PERIODIC,
        .work = cost * 100u, .period = period, .deadline = deadline, .criticality = 1};
    events[count - 1u].cost_ticks = cost;
}

/* Carga variable, rechazo, falla y una misma tarea presente en dos nodos */
static void fixture(void)
{
    count = 0u;
    accept(1u, 1u, 0u, 2u, 10u, 4u);
    add(EVENT_NODE_STARTED, 1u, 0u, 0u, 0u, 0u);
    add(EVENT_TASK_RELEASED, 1u, 1u, 1u, 0u, 0u);
    add(EVENT_IDLE_SLICE, 1u, 0u, 0u, 0u, 1u);
    add(EVENT_EXECUTION_SLICE, 1u, 1u, 1u, 1u, 2u);
    add(EVENT_TASK_REJECTED, 1u, 3u, 0u, 2u, 0u);
    events[count - 1u].detail = REJECT_INSUFFICIENT_CAPACITY;
    add(EVENT_TASK_REJECTED, 0u, 15u, 0u, 0u, 0u);
    events[count - 1u].detail = REJECT_SPORADIC_LIMIT;
    add(EVENT_TASK_COMPLETED, 1u, 1u, 1u, 3u, 0u);
    add(EVENT_IDLE_SLICE, 1u, 0u, 0u, 3u, 1u);
    accept(1u, 2u, 4u, 1u, 5u, 3u);
    add(EVENT_TASK_RELEASED, 1u, 2u, 1u, 4u, 0u);
    add(EVENT_TASK_SWITCH, 1u, 2u, 1u, 4u, 0u);
    add(EVENT_EXECUTION_SLICE, 1u, 2u, 1u, 4u, 1u);
    add(EVENT_TASK_COMPLETED, 1u, 2u, 1u, 5u, 0u);
    add(EVENT_TASK_RELEASED, 1u, 1u, 2u, 5u, 0u);
    add(EVENT_IDLE_SLICE, 1u, 0u, 0u, 5u, 1u);
    add(EVENT_TASK_SWITCH, 1u, 1u, 2u, 6u, 0u);
    add(EVENT_EXECUTION_SLICE, 1u, 1u, 2u, 6u, 1u);
    add(EVENT_IDLE_SLICE, 1u, 0u, 0u, 7u, 3u);
    add(EVENT_DEADLINE_MISSED, 1u, 1u, 2u, 9u, 0u);
    add(EVENT_NODE_STOPPED, 1u, 0u, 0u, 10u, 0u);
    accept(2u, 1u, 0u, 2u, 10u, 4u);
    add(EVENT_NODE_STARTED, 2u, 0u, 0u, 0u, 0u);
    add(EVENT_EXECUTION_SLICE, 2u, 1u, 1u, 0u, 2u);
    add(EVENT_IDLE_SLICE, 2u, 0u, 0u, 2u, 1u);
    add(EVENT_NODE_FAILED, 2u, 0u, 0u, 3u, 0u);
    add(EVENT_SERIAL_BAD_FRAME, 2u, 0u, 0u, 3u, 0u);
    events[count - 1u].detail = 4;

    /* Dos ticks distintos llegan fuera de orden; los empates conservan su orden */
    Event temporary = events[6];
    events[6] = events[18];
    events[18] = temporary;
}

static const NodeStatistics *node(const ReportStatistics *statistics, uint32_t id)
{
    for (size_t i = 0; i < statistics->node_count; ++i)
        if (statistics->nodes[i].node_id == id) return &statistics->nodes[i];
    assert(0); return NULL;
}

static const TaskStatistics *task(const ReportStatistics *statistics, uint32_t n, uint32_t id)
{
    for (size_t i = 0; i < statistics->task_count; ++i)
        if (statistics->tasks[i].node_id == n && statistics->tasks[i].task_id == id)
            return &statistics->tasks[i];
    assert(0); return NULL;
}

/* Verificar resultados calculados a mano, incluida la integral de carga C/T */
static void test_statistics(void)
{
    ReportStatistics statistics = {0};
    assert(statistics_build(events, count, &statistics));
    const NodeStatistics *first = node(&statistics, 1u);
    assert(first->observed_ticks == 10u && first->busy_ticks == 4u && first->idle_ticks == 6u);
    assert(first->switches == 2u && first->overlapping_slices == 0u);
    assert(fabsl(first->theoretical_mean - 0.32L) < 0.000001L);
    assert(fabsl(first->theoretical_peak - 0.4L) < 0.000001L);
    assert(fabsl(first->theoretical_final - 0.4L) < 0.000001L);
    const TaskStatistics *one = task(&statistics, 1u, 1u);
    assert(one->accepted == 1u && one->releases == 2u && one->execution_ticks == 3u);
    assert(one->completed == 1u && one->deadline_misses == 1u && one->slices == 2u);
    assert(one->response_count == 1u && one->response_sum == 3.0L);
    assert(one->latency_count == 2u && one->latency_sum == 2.0L);
    assert(task(&statistics, 1u, 3u)->rejected == 1u);
    assert(task(&statistics, 0u, 15u)->rejected == 1u);
    assert(statistics.node_count == 2u);
    assert(task(&statistics, 2u, 1u)->node_failures == 1u);
    const NodeStatistics *second = node(&statistics, 2u);
    assert(second->observed_ticks == 3u && second->busy_ticks == 2u);
    assert(second->failures == 1u && second->theoretical_final == 0.0L);
    assert(second->bad_frames == 4u);
    statistics_free(&statistics);

    /* Los tramos superpuestos se detectan y no duplican tiempo ocupado */
    Event overlap[] = {
        {.type = EVENT_NODE_STARTED, .node_id = 1u},
        {.type = EVENT_EXECUTION_SLICE, .node_id = 1u, .tick = 0u, .duration_ticks = 3u},
        {.type = EVENT_EXECUTION_SLICE, .node_id = 1u, .tick = 1u, .duration_ticks = 3u},
        {.type = EVENT_NODE_STOPPED, .node_id = 1u, .tick = 4u}
    };
    assert(statistics_build(overlap, 4u, &statistics));
    assert(node(&statistics, 1u)->busy_ticks == 4u);
    assert(node(&statistics, 1u)->overlapping_slices == 1u);
    statistics_free(&statistics);
    overlap[1].tick = UINT64_MAX;
    assert(!statistics_build(overlap, 4u, &statistics));

    /* Muchas instancias fuerzan el crecimiento del índice de jobs */
    Event *many = calloc(3000u, sizeof(*many));
    assert(many != NULL);
    for (uint32_t i = 0u; i < 1000u; ++i) {
        many[i * 3u] = (Event){.type = EVENT_TASK_RELEASED,
            .node_id = 1u, .task_id = 1u, .job_id = i, .tick = i * 3u};
        many[i * 3u + 1u] = (Event){.type = EVENT_EXECUTION_SLICE,
            .node_id = 1u, .task_id = 1u, .job_id = i, .tick = i * 3u + 1u, .duration_ticks = 1u};
        many[i * 3u + 2u] = (Event){.type = EVENT_TASK_COMPLETED,
            .node_id = 1u, .task_id = 1u, .job_id = i, .tick = i * 3u + 2u};
    }
    assert(statistics_build(many, 3000u, &statistics));
    assert(task(&statistics, 1u, 1u)->response_sum == 2000.0L);
    assert(task(&statistics, 1u, 1u)->latency_sum == 1000.0L);
    assert(node(&statistics, 1u)->missing_cost);
    statistics_free(&statistics);
    free(many);
}

static void test_unassigned_rejection_is_not_a_node(void)
{
    Event rejected = {
        .type = EVENT_TASK_REJECTED,
        .node_id = 0u,
        .task_id = 15u,
        .detail = REJECT_SPORADIC_LIMIT
    };
    ReportStatistics statistics = {0};
    assert(statistics_build(&rejected, 1u, &statistics));
    assert(statistics.node_count == 0u);
    assert(statistics.task_count == 1u);
    assert(task(&statistics, 0u, 15u)->rejected == 1u);
    statistics_free(&statistics);
}

static char *load(const char *directory, const char *name)
{
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", directory, name);
    FILE *file = fopen(path, "rb");
    assert(file != NULL);
    assert(fseek(file, 0, SEEK_END) == 0);
    long length = ftell(file);
    assert(length >= 0 && fseek(file, 0, SEEK_SET) == 0);
    char *text = calloc((size_t)length + 1u, 1u);
    assert(text != NULL);
    assert(fread(text, 1u, (size_t)length, file) == (size_t)length);
    assert(fclose(file) == 0);
    return text;
}

/* El consumidor recibe todos los datos por mmap compartido desde otro proceso */
static void test_report(bool invalid, bool empty)
{
    char directory[] = "/tmp/so_report_test_XXXXXX";
    assert(mkdtemp(directory) != NULL);
    EventQueue *queue = event_queue_create();
    assert(queue != NULL);
    pid_t child = fork();
    assert(child >= 0);
    if (child == 0) _exit(report_run(queue, directory));
    if (!empty) {
        for (size_t i = 0; i < count; ++i)
            assert(event_queue_push(queue, &events[i], 2000) == EVENT_QUEUE_OK);
    }
    if (invalid) {
        Event event = {.type = (EventType)99};
        assert(event_queue_push(queue, &event, 2000) == EVENT_QUEUE_OK);
    }
    assert(event_queue_close(queue) == EVENT_QUEUE_OK);
    int status;
    assert(waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == (invalid ? 1 : 0));
    char *html = load(directory, "report.html");
    char *csv = load(directory, "events.csv");
    assert(strstr(html, "Gantt por nodo") != NULL);
    assert(strstr(html, "<h3>Nodo 0</h3>") == NULL);
    assert(strstr(html, "Estadísticas por tarea") != NULL);
    assert((strstr(html, "REPORTE INCOMPLETO") != NULL) == invalid);
    if (!empty) {
        assert(strstr(html, "Sin asignar") != NULL);
        assert(strstr(html, "Plazo absoluto") != NULL);
        assert(strstr(html, "FALLA_NODO") != NULL);
        assert(strstr(html, "Capacidad insuficiente") != NULL);
        assert(strstr(html, "32.00%") != NULL && strstr(html, "40.00%") != NULL);
        assert(strstr(csv, "Capacidad insuficiente") != NULL);
    }
    size_t lines = 0u;
    for (char *p = csv; *p != '\0'; ++p) if (*p == '\n') ++lines;
    assert(lines == (empty ? 1u : count + 1u));
    free(html); free(csv);
    event_queue_destroy(queue);
    char path[256];
    snprintf(path, sizeof(path), "%s/report.html", directory); assert(unlink(path) == 0);
    snprintf(path, sizeof(path), "%s/events.csv", directory); assert(unlink(path) == 0);
    assert(rmdir(directory) == 0);
}

int main(void)
{
    fixture();
    test_statistics();
    test_unassigned_rejection_is_not_a_node();
    test_report(false, false);
    test_report(true, false);
    test_report(false, true);
    puts("OK: report and statistics tests");
    return 0;
}
