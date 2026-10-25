#include "host/report.h"
#include "host/gantt.h"

#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* El proceso conserva su propio historial; nunca consulta memoria de los drivers */
typedef struct {
    Event *events;
    size_t count;
    size_t capacity;
    size_t omitted;
    bool queue_failed;
} ReportLog;

static bool append_event(ReportLog *log, const Event *event)
{
    if (event->type < EVENT_TASK_ACCEPTED || event->type > EVENT_TASK_SWITCH ||
        UINT64_MAX - event->tick < event->duration_ticks) return false;
    if (log->count == log->capacity) {
        size_t capacity = log->capacity == 0u ? 512u : log->capacity * 2u;
        if (capacity < log->capacity || capacity > SIZE_MAX / sizeof(Event)) return false;
        Event *events = realloc(log->events, capacity * sizeof(*events));
        if (events == NULL) return false;
        log->events = events;
        log->capacity = capacity;
    }
    log->events[log->count++] = *event;
    return true;
}

/* CSV conserva los valores numéricos originales y el orden de recepción */
static bool write_csv(FILE *file, const ReportLog *log)
{
    fputs("sequence,event,source,node_id,task_id,job_id,tick,host_time_ns,duration_ticks,"
          "cost_ticks,ut_per_tick,detail,reason,task_definition_id,task_type,work,period,deadline,criticality\n", file);
    for (size_t i = 0; i < log->count; ++i) {
        const Event *event = &log->events[i];
        fprintf(file, "%zu,%s,%d,%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu64
                ",%" PRIu64 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRId32
                ",%s,%" PRIu32 ",%d,%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRId32 "\n",
                i, report_event_name(event->type), (int)event->source, event->node_id,
                event->task_id, event->job_id, event->tick, event->host_time_ns,
                event->duration_ticks, event->cost_ticks, event->ut_per_tick, event->detail,
                event->type == EVENT_TASK_REJECTED ? report_reject_reason(event->detail) : "",
                event->task.id, (int)event->task.type, event->task.work, event->task.period,
                event->task.deadline, event->task.criticality);
    }
    return !ferror(file);
}

/* Mostrar N/D cuando no hay datos suficientes, sin presentar ceros inventados */
static void write_nodes(FILE *file, const ReportStatistics *statistics)
{
    fputs("<h2>Utilización por nodo</h2><div class=table><table><thead><tr>"
          "<th>Nodo</th><th>Ticks activos</th><th>Ocupados</th><th>Idle registrado</th>"
          "<th>Teórica media</th><th>Teórica pico</th><th>Teórica final</th><th>Medida</th>"
          "<th>Cambios</th><th>Fallas</th><th>Tramas inválidas</th><th>Calidad</th></tr></thead><tbody>", file);
    for (size_t i = 0; i < statistics->node_count; ++i) {
        const NodeStatistics *node = &statistics->nodes[i];
        fprintf(file, "<tr><td>%" PRIu32 "</td><td>%" PRIu64 "</td><td>%" PRIu64
                "</td><td>%" PRIu64 "</td>", node->node_id, node->observed_ticks,
                node->busy_ticks, node->idle_ticks);
        if (node->missing_cost) fputs("<td>N/D</td><td>N/D</td><td>N/D</td>", file);
        else {
            if (node->observed_ticks == 0u) fputs("<td>N/D</td>", file);
            else fprintf(file, "<td>%.2Lf%%</td>", node->theoretical_mean * 100.0L);
            fprintf(file, "<td>%.2Lf%%</td><td>%.2Lf%%</td>",
                    node->theoretical_peak * 100.0L, node->theoretical_final * 100.0L);
        }
        if (node->observed_ticks == 0u || node->overlapping_slices > 0u ||
            node->out_of_window_slices > 0u || node->busy_ticks > node->observed_ticks ||
            node->idle_ticks != node->observed_ticks - node->busy_ticks)
            fputs("<td>N/D</td>", file);
        else fprintf(file, "<td>%.2Lf%%</td>",
                      (long double)node->busy_ticks / node->observed_ticks * 100.0L);
        fprintf(file, "<td>%" PRIu64 "</td><td>%" PRIu64 "</td><td>%" PRIu64 "</td><td>",
                node->switches, node->failures, node->bad_frames);
        if (node->implicit_start) fputs("Inicio inferido. ", file);
        if (node->missing_cost) fputs("Falta definición/costo. ", file);
        if (node->overlapping_slices > 0u) fputs("Hay tramos superpuestos. ", file);
        if (node->out_of_window_slices > 0u) fputs("Hay tramos fuera del tiempo activo. ", file);
        if (node->busy_ticks > node->observed_ticks) fputs("Ejecución fuera del tiempo activo. ", file);
        if (node->busy_ticks <= node->observed_ticks && node->idle_ticks < node->observed_ticks - node->busy_ticks)
            fputs("Hay tiempo sin tramos registrados. ", file);
        fputs("</td></tr>", file);
    }
    fputs("</tbody></table></div><p>Teórica: suma C/T de las asignaciones confirmadas. "
          "La media integra esa carga durante los ticks activos; el pico y la carga final "
          "conservan los cambios de asignación. Medida: unión de ticks ejecutados / ticks activos. "
          "Sin intervalos de ejecución no se puede reconstruir el trabajo a partir de reportes de tick.</p>", file);
}

static void write_tasks(FILE *file, const ReportStatistics *statistics)
{
    fputs("<h2>Estadísticas por tarea y nodo</h2><div class=table><table><thead><tr>"
          "<th>Nodo</th><th>Tarea</th><th>Tipo</th><th>C / T / D</th><th>Aceptadas</th><th>Rechazadas</th>"
          "<th>Liberaciones</th><th>Tramos</th><th>Ticks ejecutados</th><th>Completadas</th>"
          "<th>Plazos perdidos</th><th>Esporádicas ignoradas</th><th>Cambios entrantes</th>"
          "<th>Fallas del nodo asignado</th><th>Respuesta media</th><th>Latencia inicial media</th>"
          "</tr></thead><tbody>", file);
    for (size_t i = 0; i < statistics->task_count; ++i) {
        const TaskStatistics *task = &statistics->tasks[i];
        fprintf(file, "<tr><td>%" PRIu32 "</td><td>%" PRIu32 "</td>", task->node_id, task->task_id);
        if (task->definition_known) {
            fprintf(file, "<td>%c</td><td>", (char)task->task.type);
            if (task->cost_known) fprintf(file, "%" PRIu32, task->cost_ticks);
            else fputs("N/D", file);
            fprintf(file, " / %" PRIu32 " / %" PRIu32 "</td>", task->task.period, task->task.deadline);
        }
        else fputs("<td>N/D</td><td>N/D</td>", file);
        fprintf(file, "<td>%" PRIu64 "</td><td>%" PRIu64 "</td><td>%" PRIu64
                "</td><td>%" PRIu64 "</td><td>%" PRIu64 "</td><td>%" PRIu64
                "</td><td>%" PRIu64 "</td><td>%" PRIu64 "</td><td>%" PRIu64 "</td><td>%" PRIu64 "</td>",
                task->accepted, task->rejected, task->releases, task->slices, task->execution_ticks,
                task->completed, task->deadline_misses, task->ignored, task->switches, task->node_failures);
        if (task->response_count > 0u) fprintf(file, "<td>%.2Lf ticks (%" PRIu64 " jobs)</td>",
                task->response_sum / task->response_count, task->response_count);
        else fputs("<td>N/D</td>", file);
        if (task->latency_count > 0u) fprintf(file, "<td>%.2Lf ticks (%" PRIu64 " jobs)</td>",
                task->latency_sum / task->latency_count, task->latency_count);
        else fputs("<td>N/D</td>", file);
        fputs("</tr>", file);
    }
    fputs("</tbody></table></div><p>Los contadores corresponden a eventos recibidos. "
          "Respuesta = finalización − liberación; latencia inicial = primera ejecución − liberación. "
          "Solo se promedian instancias con liberación conocida. Una tarea migrada conserva una fila por nodo.</p>", file);
}

static void write_history(FILE *file, const ReportLog *log)
{
    fputs("<h2>Tareas aceptadas y rechazadas</h2><div class=table><table><thead><tr>"
          "<th>Tick</th><th>Nodo</th><th>Tarea</th><th>Decisión</th><th>Motivo</th><th>Costo</th>"
          "</tr></thead><tbody>", file);
    for (size_t i = 0; i < log->count; ++i) {
        const Event *event = &log->events[i];
        if (event->type != EVENT_TASK_ACCEPTED && event->type != EVENT_TASK_REJECTED) continue;
        fprintf(file, "<tr><td>%" PRIu64 "</td><td>%" PRIu32 "</td><td>%" PRIu32
                "</td><td>%s</td><td>%s (%" PRId32 ")</td><td>%" PRIu32 "</td></tr>",
                event->tick, event->node_id, event->task_id,
                event->type == EVENT_TASK_ACCEPTED ? "Aceptada" : "Rechazada",
                event->type == EVENT_TASK_ACCEPTED ? "Admisión confirmada" : report_reject_reason(event->detail),
                event->detail, event->cost_ticks);
    }
    fputs("</tbody></table></div><h2>Liberaciones, ejecución, plazos, cambios y fallas</h2>"
          "<div class=table><table><thead><tr><th>Secuencia</th><th>Tick</th><th>Evento</th>"
          "<th>Nodo</th><th>Tarea</th><th>Job</th><th>Duración</th><th>Detalle</th></tr></thead><tbody>", file);
    for (size_t i = 0; i < log->count; ++i) {
        const Event *event = &log->events[i];
        fprintf(file, "<tr><td>%zu</td><td>%" PRIu64 "</td><td>%s</td><td>%" PRIu32
                "</td><td>%" PRIu32 "</td><td>%" PRIu32 "</td><td>%" PRIu32 "</td><td>%" PRId32 "</td></tr>",
                i, event->tick, report_event_name(event->type), event->node_id,
                event->task_id, event->job_id, event->duration_ticks, event->detail);
    }
    fputs("</tbody></table></div>", file);
}

static bool write_html(FILE *file, const ReportLog *log, const ReportStatistics *statistics)
{
    fputs("<!doctype html><html lang=es><head><meta charset=utf-8>"
          "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
          "<title>Reporte de ejecución</title><style>"
          "body{font:15px system-ui,sans-serif;color:#172033;background:#f1f5f9;margin:0}"
          "main{max-width:1440px;margin:auto;padding:32px}h1{font-size:32px}h2{margin-top:36px}"
          "p{line-height:1.6;max-width:1000px}.table,.chart{overflow:auto;background:white;"
          "border:1px solid #cbd5e1;border-radius:8px;margin:16px 0}table{border-collapse:collapse;"
          "width:100%;white-space:nowrap;font-size:13px}th,td{padding:10px 14px;text-align:left;"
          "border-bottom:1px solid #e2e8f0}th{background:#e2e8f0}svg{display:block;width:100%;"
          "min-width:1000px}.warning{padding:14px;background:#fef3c7;border-left:4px solid #d97706}"
          "a{color:#1d4ed8}@media print{main{padding:0}.table,.chart{overflow:visible}svg{min-width:0}}"
          "</style></head><body><main><h1>Reporte de ejecución</h1>", file);
    fprintf(file, "<p>%zu eventos · %zu nodos · %zu parejas nodo/tarea. "
            "Datos recibidos exclusivamente mediante la cola de eventos en memoria compartida. "
            "<a href=\"events.csv\">Descargar eventos CSV</a>.</p>", log->count,
            statistics->node_count, statistics->task_count);
    if (log->omitted > 0u || log->queue_failed)
        fprintf(file, "<p class=warning>REPORTE INCOMPLETO: %zu eventos omitidos; "
                "falla de cola/productor: %s. No interpretar las estadísticas como una ejecución completa.</p>",
                log->omitted, log->queue_failed ? "sí" : "no");
    fputs("<h2>Gantt por nodo</h2><p>Escala en ticks. Barras de color: ejecución; gris: idle. "
          "L: liberación; C: finalización; D: plazo calculado; !: incumplimiento reportado; "
          "F: falla; S: cambio de tarea; Q: parada. Los detalles aparecen al pasar el cursor.</p>", file);
    if (!gantt_write(file, log->events, log->count, statistics)) return false;
    write_nodes(file, statistics);
    write_tasks(file, statistics);
    write_history(file, log);
    fputs("</main></body></html>\n", file);
    return !ferror(file);
}

/* Publicar cada archivo mediante rename para no dejar un archivo final a medio escribir */
static bool export_report(const char *directory, const ReportLog *log)
{
    if (mkdir(directory, 0775) != 0 && errno != EEXIST) return false;
    ReportStatistics statistics = {0};
    if (!statistics_build(log->events, log->count, &statistics)) return false;
    const char *names[] = {"events.csv", "report.html"};
    bool success = true;
    for (size_t i = 0; i < 2u && success; ++i) {
        size_t size = strlen(directory) + 64u;
        char *path = malloc(size), *temporary = malloc(size);
        if (path == NULL || temporary == NULL) {
            free(path); free(temporary); success = false; break;
        }
        snprintf(path, size, "%s/%s", directory, names[i]);
        snprintf(temporary, size, "%s/.%s.%ld.tmp", directory, names[i], (long)getpid());
        FILE *file = fopen(temporary, "w");
        if (file == NULL) success = false;
        else {
            success = i == 0u ? write_csv(file, log) : write_html(file, log, &statistics);
            if (fclose(file) != 0) success = false;
            if (success && rename(temporary, path) != 0) success = false;
            if (!success) unlink(temporary);
        }
        free(path); free(temporary);
    }
    statistics_free(&statistics);
    return success;
}

int report_run(EventQueue *queue, const char *directory)
{
    if (queue == NULL || directory == NULL || directory[0] == '\0') return 1;
    ReportLog log = {0};
    Event event;
    EventQueueResult result;
    while ((result = event_queue_pop(queue, &event, -1)) == EVENT_QUEUE_OK) {
        if (!append_event(&log, &event)) {
            ++log.omitted;
            event_queue_request_stop(queue);
        }
    }
    log.queue_failed = result != EVENT_QUEUE_CLOSED || event_queue_is_incomplete(queue);
    bool success = export_report(directory, &log);
    free(log.events);
    return success && log.omitted == 0u && !log.queue_failed ? 0 : 1;
}
