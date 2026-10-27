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
    /* El worker recibe comandos por stdin; la sesión deja de leerlo en el padre. */
    bool worker_reads_stdin;
} ReportWorker;

typedef int (*ReportCoordinatorRun)(EventQueue *events, void *context);
/*
 * Configures inherited child resources before work begins. reporter is true
 * for the reporting process; otherwise worker_index identifies a worker.
 */
typedef int (*ReportChildSetup)(bool reporter, size_t worker_index, void *context);
typedef int (*ReportParentSetup)(void *context);

/*
 * Crea un proceso de reportes y un proceso por worker.
 * q, EOF, SIGINT o SIGTERM solicitan un cierre ordenado.
 * Orden: solicitar parada -> stop de drivers -> waitpid -> cerrar/drenar cola
 * -> exportar reporte -> esperar reportes -> restaurar terminal/liberar cola.
 * Retorna 0 en un cierre completo y 1 si hubo errores.
 */
int report_session_run(const char *directory, const ReportWorker *workers, size_t count);

/*
 * Runs the coordinator in the parent process while workers and reporter run
 * as children. The coordinator can exchange application commands with workers
 * and returns when the session should shut down.
 * child_setup closes/configures resources inherited by each child process.
 * parent_setup closes resources the parent does not need after forking.
 */
int report_session_run_coordinated(
    const char *directory,
    const ReportWorker *workers,
    size_t count,
    ReportCoordinatorRun coordinator,
    ReportChildSetup child_setup,
    ReportParentSetup parent_setup,
    void *context
);

/* Demostración con dos nodos simulados; no abre ni controla dispositivos */
int report_demo_run(const char *directory);

#endif // HOST_REPORT_SESSION_H
