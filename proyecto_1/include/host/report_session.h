#ifndef HOST_REPORT_SESSION_H
#define HOST_REPORT_SESSION_H

#include <stddef.h>
#include "host/event_queue.h"

/*
 * Adaptador de un driver/productor. run debe consultar stop_requested y usar
 * operaciones de transporte con tiempo límite. stop se invoca siempre al salir
 * de run: envía STOP, espera su confirmación, publica eventos finales y libera
 * los recursos del driver. Ambos retornan cero si finalizan correctamente.
 */
typedef struct {
    int (*run)(EventQueue *events, void *context);
    int (*stop)(EventQueue *events, void *context);
    void *context;
} ReportWorker;

/*
 * Crea un proceso de reportes y un proceso por worker.
 * q, EOF, SIGINT o SIGTERM solicitan un cierre ordenado.
 * Orden: solicitar parada -> stop de drivers -> waitpid -> cerrar/drenar cola
 * -> exportar reporte -> esperar reportes -> restaurar terminal/liberar cola.
 * Retorna 0 en un cierre completo y 1 si hubo errores.
 */
int report_session_run(const char *directory, const ReportWorker *workers, size_t count);

/* Demostración con dos nodos simulados; no abre ni controla dispositivos */
int report_demo_run(const char *directory);

#endif // HOST_REPORT_SESSION_H
