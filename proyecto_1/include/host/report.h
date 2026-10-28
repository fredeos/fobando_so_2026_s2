#ifndef HOST_REPORT_H
#define HOST_REPORT_H

#include "host/event_queue.h"

/*
 * Entrada del proceso de reportes. Su única fuente de datos es la cola.
 * Consume hasta CLOSED, genera report.html y events.csv en directory.
 * Retorna 0 si el reporte está completo; 1 ante errores o pérdida de datos.
 * No cierra ni destruye la cola: esa responsabilidad es del coordinador.
 */
int report_run(EventQueue *queue, const char *directory);

#endif // HOST_REPORT_H
