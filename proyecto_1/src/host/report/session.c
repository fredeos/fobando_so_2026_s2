#include "host/report_session.h"
#include "host/report.h"

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/* La señal solo activa esta bandera; el cierre se hace luego en el flujo normal. */
static volatile sig_atomic_t interrupted;

/** @brief Marca que llegó SIGINT o SIGTERM para iniciar el cierre ordenado. */
static void request_interrupt(int signal_number)
{
    (void)signal_number;
    interrupted = 1;
}

/** @brief Devuelve el tiempo monotónico actual en milisegundos. */
static uint64_t monotonic_ms(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
}

/** @brief Pausa brevemente entre sondeos de hijos para no consumir CPU en espera activa. */
static void short_pause(void)
{
    struct timespec pause = {.tv_nsec = 10000000L};
    nanosleep(&pause, NULL);
}

/** @brief Indica si un hijo terminó con código de salida cero. */
static bool successful(int status)
{
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

/** @brief Recoge hijos que ya terminaron sin bloquear la supervisión de la sesión. */
static size_t reap_workers(pid_t *pids, size_t count, bool *failed)
{
    size_t alive = 0u;
    for (size_t i = 0; i < count; ++i) {
        if (pids[i] == 0) continue;
        int status;
        /* WNOHANG permite revisar todos los hijos y seguir atendiendo el cierre. */
        pid_t result = waitpid(pids[i], &status, WNOHANG);
        if (result == pids[i]) {
            if (!successful(status)) *failed = true;
            pids[i] = 0;
        } else if (result < 0 && errno != EINTR) {
            *failed = true;
            pids[i] = 0;
        } else ++alive;
    }
    return alive;
}

/**
 * @brief Solicita el cierre de workers y aplica límites si alguno no responde.
 * El cierre normal es cooperativo; un timeout marca el reporte incompleto.
 */
static void finish_workers(
    EventQueue *queue,
    pid_t *pids,
    size_t count,
    bool *failed,
    bool worker_reads_stdin,
    bool coordinated
)
{
    /* La cola avisa a los workers cooperativos que deben salir de su bucle. */
    event_queue_request_stop(queue);
    uint64_t start = monotonic_ms();
    uint64_t graceful_timeout = worker_reads_stdin ? 7000u
        : coordinated ? 15000u : 3000u;
    bool terminated = false, killed = false;
    while (reap_workers(pids, count, failed) > 0u) {
        uint64_t elapsed = monotonic_ms() - start;
        if (elapsed >= graceful_timeout && !terminated) {
            /* Primera escalada: dar una señal de terminación al proceso atascado. */
            *failed = true;
            event_queue_mark_incomplete(queue);
            for (size_t i = 0; i < count; ++i) if (pids[i] > 0) kill(pids[i], SIGTERM);
            terminated = true;
        }
        if (elapsed >= graceful_timeout + 500u && !killed) {
            /* Último recurso para evitar que la sesión quede esperando para siempre. */
            for (size_t i = 0; i < count; ++i) if (pids[i] > 0) kill(pids[i], SIGKILL);
            killed = true;
        }
        short_pause();
    }
}

/**
 * @brief Crea la cola y los procesos, coordina su cierre y espera sus resultados.
 * Si coordinator no es NULL, el padre ejecuta esa función; de lo contrario,
 * supervisa la entrada estándar para cerrar la sesión.
 */
static int report_session_run_internal(
    const char *directory,
    const ReportWorker *workers,
    size_t count,
    ReportCoordinatorRun coordinator,
    ReportChildSetup child_setup,
    ReportParentSetup parent_setup,
    void *coordinator_context
)
{
    /* Validar callbacks y argumentos antes de crear recursos compartidos. */
    /* Rechazar configuraciones incompletas antes de reservar memoria o hacer fork. */
    if (directory == NULL || directory[0] == '\0' || workers == NULL || count == 0u ||
        count > SIZE_MAX / sizeof(pid_t)) return 1;
    size_t stdin_workers = 0u;
    for (size_t i = 0; i < count; ++i) {
        if (workers[i].run == NULL || workers[i].stop == NULL) return 1;
        if (workers[i].worker_reads_stdin) ++stdin_workers;
    }
    if (stdin_workers > 1u) return 1;

    struct stat directory_info;
    if ((mkdir(directory, 0775) != 0 && errno != EEXIST) ||
        stat(directory, &directory_info) != 0 || !S_ISDIR(directory_info.st_mode))
        return 1;

    /* Reporter y workers compartirán esta cola heredada después de fork(). */
    /* La cola debe existir antes de crear hijos para que todos hereden su acceso. */
    EventQueue *queue = event_queue_create();
    pid_t *pids = calloc(count, sizeof(*pids));
    if (queue == NULL || pids == NULL) {
        event_queue_destroy(queue); free(pids); return 1;
    }

    bool worker_reads_stdin = false;
    for (size_t i = 0u; i < count; ++i)
        worker_reads_stdin = worker_reads_stdin || workers[i].worker_reads_stdin;

    struct termios original;
    bool terminal_changed = false, failed = false;
    if (isatty(STDIN_FILENO) && !worker_reads_stdin && coordinator == NULL) {
        if (tcgetattr(STDIN_FILENO, &original) != 0) goto setup_fail;
        struct termios immediate = original;
        immediate.c_lflag &= (tcflag_t)~(ICANON | ECHO);
        immediate.c_cc[VMIN] = 1;
        immediate.c_cc[VTIME] = 0;
        if (tcsetattr(STDIN_FILENO, TCSANOW, &immediate) != 0) goto setup_fail;
        terminal_changed = true;
    }

    /* Guardar y reemplazar señales para poder cerrar hijos y restaurarlas luego. */
    struct sigaction action = {0}, old_int, old_term, old_pipe;
    action.sa_handler = request_interrupt;
    sigemptyset(&action.sa_mask);
    interrupted = 0;
    if (sigaction(SIGINT, &action, &old_int) != 0) goto setup_fail;
    if (sigaction(SIGTERM, &action, &old_term) != 0) {
        sigaction(SIGINT, &old_int, NULL);
        goto setup_fail;
    }
    struct sigaction ignore_pipe = {0};
    ignore_pipe.sa_handler = SIG_IGN;
    sigemptyset(&ignore_pipe.sa_mask);
    if (sigaction(SIGPIPE, &ignore_pipe, &old_pipe) != 0) {
        sigaction(SIGINT, &old_int, NULL);
        sigaction(SIGTERM, &old_term, NULL);
        goto setup_fail;
    }
    /* Vaciar buffers antes de fork para no duplicar salida en los procesos hijos. */
    fflush(NULL);
    pid_t reporter = fork();
    if (reporter == 0) {
        /* El reporter solo consume eventos y exporta el informe. */
        signal(SIGINT, SIG_IGN);
        signal(SIGTERM, SIG_DFL);
        if (child_setup != NULL && child_setup(true, 0u, coordinator_context) != 0) {
            event_queue_mark_incomplete(queue);
            _exit(1);
        }
        _exit(report_run(queue, directory));
    }
    if (reporter < 0) failed = true;

    for (size_t i = 0; i < count && !failed; ++i) {
        pids[i] = fork();
        if (pids[i] == 0) {
            /* Cada hijo ejecuta un único worker y luego su callback de limpieza. */
            signal(SIGINT, SIG_IGN);
            signal(SIGTERM, SIG_DFL);
            if (child_setup != NULL &&
                child_setup(false, i, coordinator_context) != 0) {
                event_queue_mark_incomplete(queue);
                _exit(1);
            }
            int result = workers[i].run(queue, workers[i].context);
            int stopped = workers[i].stop(queue, workers[i].context);
            if (result != 0 || stopped != 0) event_queue_mark_incomplete(queue);
            _exit(result == 0 && stopped == 0 ? 0 : 1);
        }
        if (pids[i] < 0) { pids[i] = 0; failed = true; }
    }

    /* Cerrar en el padre los extremos de pipes que pertenecen a los hijos. */
    if (parent_setup != NULL && parent_setup(coordinator_context) != 0)
        failed = true;

    bool reporter_reaped = false;
    if (!failed && coordinator != NULL) {
        /* En modo coordinado, el padre atiende la consola y las órdenes de nodo. */
        if (coordinator(queue, coordinator_context) != 0)
            failed = true;
        event_queue_request_stop(queue);
    }

    while (!failed && !interrupted && !event_queue_stop_requested(queue)) {
        /* Supervisar hijos y detectar q/señales cuando la sesión no tiene coordinador. */
        if (reap_workers(pids, count, &failed) == 0u || failed) break;
        int status;
        if (waitpid(reporter, &status, WNOHANG) == reporter) {
            reporter_reaped = true;
            failed = true;
            break;
        }
        if (worker_reads_stdin) {
            short_pause();
            continue;
        }
        struct pollfd input = {.fd = STDIN_FILENO, .events = POLLIN};
        int ready = poll(&input, 1u, 50);
        if (ready < 0) {
            if (errno != EINTR) failed = true;
        } else if (ready > 0) {
            if (input.revents & (POLLERR | POLLNVAL)) { failed = true; break; }
            if (input.revents & (POLLIN | POLLHUP)) {
                char keys[64];
                ssize_t length = read(STDIN_FILENO, keys, sizeof(keys));
                if (length == 0) break;
                if (length < 0 && errno != EINTR) { failed = true; break; }
                for (ssize_t i = 0; i < length; ++i)
                    if (keys[i] == 'q') interrupted = 1;
            }
        }
    }

    /* Cerrar productores primero para que el reporter pueda drenar la cola. */
    finish_workers(queue, pids, count, &failed, worker_reads_stdin,
                   coordinator != NULL);
    /* Cerrar la cola deja que el reporter termine cuando ya drenó lo pendiente. */
    if (failed) event_queue_mark_incomplete(queue);
    if (event_queue_close(queue) != EVENT_QUEUE_OK) failed = true;

    /* Esperar al reporter después de cerrar la cola para que finalice el informe. */
    if (reporter > 0 && !reporter_reaped) {
        uint64_t start = monotonic_ms();
        for (;;) {
            int status;
            pid_t result = waitpid(reporter, &status, WNOHANG);
            if (result == reporter) {
                if (!successful(status)) failed = true;
                break;
            }
            if (result < 0 && errno != EINTR) { failed = true; break; }
            if (monotonic_ms() - start > 30000u) {
                failed = true;
                kill(reporter, SIGKILL);
                while (waitpid(reporter, &status, 0) < 0 && errno == EINTR) {}
                break;
            }
            short_pause();
        }
    }

    /* Restaurar el estado original del proceso y liberar memoria compartida. */
    if (terminal_changed && tcsetattr(STDIN_FILENO, TCSANOW, &original) != 0) failed = true;
    sigaction(SIGINT, &old_int, NULL);
    sigaction(SIGTERM, &old_term, NULL);
    sigaction(SIGPIPE, &old_pipe, NULL);
    if (failed) event_queue_mark_incomplete(queue);
    event_queue_destroy(queue);
    free(pids);
    return failed ? 1 : 0;

setup_fail:
    /* Si falla la configuración inicial, liberar lo creado antes de retornar. */
    if (terminal_changed) tcsetattr(STDIN_FILENO, TCSANOW, &original);
    event_queue_destroy(queue);
    free(pids);
    return 1;
}

/**
 * @brief Ejecuta una sesión con supervisor que solo monitorea `q`.
 *
 * @param directory Directorio donde se generarán los reportes.
 * @param workers Procesos productores de eventos.
 * @param count Cantidad de workers.
 * @return Cero si la sesión y sus procesos terminaron correctamente.
 */
int report_session_run(const char *directory, const ReportWorker *workers, size_t count)
{
    return report_session_run_internal(
        directory, workers, count, NULL, NULL, NULL, NULL
    );
}

/**
 * @brief Ejecuta workers y reporter mientras el proceso padre los coordina.
 *
 * @param directory Directorio donde se generarán los reportes.
 * @param workers Procesos productores de eventos.
 * @param count Cantidad de workers.
 * @param coordinator Función ejecutada en el proceso padre.
 * @param context Contexto privado del coordinador.
 * @return Cero si la sesión y sus procesos terminaron correctamente.
 */
int report_session_run_coordinated(
    const char *directory,
    const ReportWorker *workers,
    size_t count,
    ReportCoordinatorRun coordinator,
    ReportChildSetup child_setup,
    ReportParentSetup parent_setup,
    void *context
)
{
    if (coordinator == NULL)
        return 1;
    return report_session_run_internal(
        directory, workers, count, coordinator, child_setup, parent_setup, context
    );
}
