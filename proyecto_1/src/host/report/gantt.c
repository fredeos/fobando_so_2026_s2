#include "host/gantt.h"

#include <inttypes.h>

/* Posicionar usando diferencias enteras antes de convertir a punto flotante */
static long double position(uint64_t tick, uint64_t first, uint64_t span)
{
    return 150.0L + (long double)(tick - first) / span * 1000.0L;
}

static bool marker(EventType type)
{
    return type == EVENT_TASK_RELEASED || type == EVENT_TASK_COMPLETED ||
           type == EVENT_DEADLINE_MISSED || type == EVENT_NODE_FAILED ||
           type == EVENT_NODE_STOPPED || type == EVENT_TASK_SWITCH;
}

/* Usar una fila por tarea y otra para inactividad y eventos del nodo */
bool gantt_write(FILE *file, const Event *events, size_t count,
                  const ReportStatistics *statistics)
{
    static const char *const colors[] = {
        "#2563eb", "#0d9488", "#9333ea", "#d97706", "#db2777", "#4f46e5"
    };
    for (size_t n = 0; n < statistics->node_count; ++n) {
        const NodeStatistics *node = &statistics->nodes[n];
        uint64_t first = UINT64_MAX, last = 0u;
        size_t rows = 1u;
        for (size_t t = 0; t < statistics->task_count; ++t)
            if (statistics->tasks[t].node_id == node->node_id) ++rows;

        for (size_t i = 0; i < count; ++i) {
            const Event *event = &events[i];
            if (event->node_id != node->node_id) continue;
            if (event->tick < first) first = event->tick;
            uint64_t end = event->tick;
            if (event->type == EVENT_EXECUTION_SLICE || event->type == EVENT_IDLE_SLICE)
                end += event->duration_ticks;
            if (end > last) last = end;
            if (event->type == EVENT_TASK_RELEASED) {
                for (size_t t = 0; t < statistics->task_count; ++t) {
                    const TaskStatistics *task = &statistics->tasks[t];
                    if (task->node_id == node->node_id && task->task_id == event->task_id &&
                        task->definition_known && UINT64_MAX - end >= task->task.deadline &&
                        end + task->task.deadline > last) last = end + task->task.deadline;
                }
            }
        }
        if (first == UINT64_MAX) first = 0u;
        uint64_t span = last > first ? last - first : 1u;
        size_t height = 75u + rows * 42u;
        fprintf(file, "<h3>Nodo %" PRIu32 "</h3><div class=chart>"
                "<svg xmlns=\"http://www.w3.org/2000/svg\" role=\"img\" font-family=\"sans-serif\" "
                "aria-label=\"Gantt del nodo %" PRIu32 "\" viewBox=\"0 0 1200 %zu\">"
                "<rect width=\"1200\" height=\"%zu\" fill=\"white\"/>",
                node->node_id, node->node_id, height, height);

        for (unsigned grid = 0u; grid <= (last > first ? 8u : 0u); ++grid) {
            /* Evitar span * grid, que podría desbordar uint64_t */
            uint64_t tick = first + (span / 8u) * grid + (span % 8u) * grid / 8u;
            long double x = position(tick, first, span);
            fprintf(file, "<line x1=\"%.2Lf\" x2=\"%.2Lf\" y1=\"30\" y2=\"%zu\" "
                    "stroke=\"#e2e8f0\"/><text x=\"%.2Lf\" y=\"20\" "
                    "text-anchor=\"%s\" font-size=\"10\">%" PRIu64 "</text>",
                    x, x, height - 15u, x, grid == 0u ? "start" : grid == 8u ? "end" : "middle", tick);
        }

        for (size_t row = 0u; row < rows; ++row) {
            const TaskStatistics *task = NULL;
            size_t current = 1u;
            if (row > 0u) {
                for (size_t t = 0; t < statistics->task_count; ++t) {
                    if (statistics->tasks[t].node_id == node->node_id && current++ == row) {
                        task = &statistics->tasks[t];
                        break;
                    }
                }
            }
            size_t y = 42u + row * 42u;
            if (task == NULL) fprintf(file, "<text x=\"10\" y=\"%zu\" font-size=\"12\">Nodo / idle</text>", y + 18u);
            else fprintf(file, "<text x=\"10\" y=\"%zu\" font-size=\"12\">Tarea %" PRIu32 "</text>", y + 18u, task->task_id);

            for (size_t i = 0; i < count; ++i) {
                const Event *event = &events[i];
                bool node_event = event->type == EVENT_IDLE_SLICE || event->type == EVENT_NODE_FAILED ||
                    event->type == EVENT_NODE_STOPPED || event->type == EVENT_TASK_SWITCH;
                if (event->node_id != node->node_id ||
                    (task == NULL ? !node_event : node_event || event->task_id != task->task_id)) continue;
                long double x = position(event->tick, first, span);

                if (event->type == EVENT_EXECUTION_SLICE || event->type == EVENT_IDLE_SLICE) {
                    long double width = (long double)event->duration_ticks / span * 1000.0L;
                    fprintf(file, "<rect x=\"%.2Lf\" y=\"%zu\" width=\"%.2Lf\" height=\"24\" "
                            "rx=\"3\" fill=\"%s\"><title>%s: tarea %" PRIu32
                            ", job %" PRIu32 ", [%" PRIu64 ", %" PRIu64 ")</title></rect>",
                            x, y, width, task == NULL ? "#cbd5e1" : colors[task->task_id % 6u],
                            report_event_name(event->type), event->task_id, event->job_id,
                            event->tick, event->tick + event->duration_ticks);
                } else if (marker(event->type)) {
                    const char *color = event->type == EVENT_DEADLINE_MISSED ||
                        event->type == EVENT_NODE_FAILED ? "#dc2626" : "#0f172a";
                    const char *label = event->type == EVENT_TASK_RELEASED ? "L" :
                        event->type == EVENT_TASK_COMPLETED ? "C" :
                        event->type == EVENT_DEADLINE_MISSED ? "!" :
                        event->type == EVENT_NODE_FAILED ? "F" :
                        event->type == EVENT_TASK_SWITCH ? "S" : "Q";
                    fprintf(file, "<line x1=\"%.2Lf\" x2=\"%.2Lf\" y1=\"%zu\" y2=\"%zu\" "
                            "stroke=\"%s\"/><text x=\"%.2Lf\" y=\"%zu\" fill=\"%s\" font-size=\"11\">%s"
                            "<title>%s en tick %" PRIu64 ", job %" PRIu32 "</title></text>",
                            x, x, y, y + 27u, color, x + 2.0L,
                            event->type == EVENT_NODE_STOPPED ? y + 12u : y + 36u, color, label,
                            report_event_name(event->type), event->tick, event->job_id);
                }

                if (event->type == EVENT_TASK_RELEASED && task != NULL && task->definition_known &&
                    UINT64_MAX - event->tick >= task->task.deadline) {
                    uint64_t deadline = event->tick + task->task.deadline;
                    long double dx = position(deadline, first, span);
                    fprintf(file, "<line x1=\"%.2Lf\" x2=\"%.2Lf\" y1=\"%zu\" y2=\"%zu\" "
                            "stroke=\"#d97706\" stroke-dasharray=\"3 2\"/>"
                            "<text x=\"%.2Lf\" y=\"%zu\" fill=\"#92400e\" font-size=\"11\">D"
                            "<title>Plazo absoluto: %" PRIu64 ", job %" PRIu32 "</title></text>",
                            dx, dx, y, y + 27u, dx + 2.0L, y + 12u, deadline, event->job_id);
                }
            }
        }
        fputs("</svg></div>", file);
    }
    return !ferror(file);
}
