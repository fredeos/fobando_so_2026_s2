#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "host/report_session.h"

typedef struct {
    uint32_t id;
    int markers;
    bool fail;
    bool hang;
} TestWorker;

static int run_worker(EventQueue *queue, void *context)
{
    TestWorker *worker = context;
    Event event = {.type = EVENT_NODE_STARTED, .node_id = worker->id};
    assert(event_queue_push(queue, &event, 1000) == EVENT_QUEUE_OK);
    assert(write(worker->markers, "R", 1u) == 1);
    if (worker->fail) return 1;
    while (worker->hang || !event_queue_stop_requested(queue)) {
        struct timespec pause = {.tv_nsec = 1000000L};
        nanosleep(&pause, NULL);
    }
    return 0;
}

/* El marcador prueba que se ejecutó la operación STOP antes de exportar */
static int stop_worker(EventQueue *queue, void *context)
{
    TestWorker *worker = context;
    Event event = {.type = EVENT_NODE_STOPPED, .node_id = worker->id, .tick = 2u};
    assert(event_queue_push(queue, &event, 1000) == EVENT_QUEUE_OK);
    assert(write(worker->markers, "S", 1u) == 1);
    return 0;
}

/* q sin Enter, EOF y SIGTERM deben seguir el mismo orden de liberación */
static void test_session(unsigned mode)
{
    char directory[] = "/tmp/so_session_test_XXXXXX";
    assert(mkdtemp(directory) != NULL);
    int input[2], markers[2];
    assert(pipe(input) == 0 && pipe(markers) == 0);
    pid_t supervisor = fork();
    assert(supervisor >= 0);
    if (supervisor == 0) {
        close(input[1]); close(markers[0]);
        assert(dup2(input[0], STDIN_FILENO) == STDIN_FILENO);
        close(input[0]);
        TestWorker contexts[] = {
            {.id = 1u, .markers = markers[1]},
            {.id = 2u, .markers = markers[1], .fail = mode == 3u, .hang = mode == 4u}
        };
        ReportWorker workers[] = {
            {.run = run_worker, .stop = stop_worker, .context = &contexts[0]},
            {.run = run_worker, .stop = stop_worker, .context = &contexts[1]}
        };
        int result = report_session_run(directory, workers, 2u);
        close(markers[1]);
        int status;
        assert(waitpid(-1, &status, WNOHANG) == -1 && errno == ECHILD);
        _exit(result);
    }
    close(input[0]); close(markers[1]);
    unsigned ready = 0u, stopped = 0u;
    while (ready < 2u) {
        char marker;
        assert(read(markers[0], &marker, 1u) == 1);
        if (marker == 'R') ++ready;
        else if (marker == 'S') ++stopped;
        else assert(0);
    }
    if (mode == 0u || mode == 4u) assert(write(input[1], "q", 1u) == 1);
    if (mode == 2u) assert(kill(supervisor, SIGTERM) == 0);
    close(input[1]);
    char marker;
    ssize_t length;
    while ((length = read(markers[0], &marker, 1u)) > 0) {
        assert(marker == 'S');
        ++stopped;
    }
    assert(length == 0 && stopped == (mode == 4u ? 1u : 2u));
    close(markers[0]);
    int status;
    assert(waitpid(supervisor, &status, 0) == supervisor);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == (mode >= 3u ? 1 : 0));

    char path[256];
    snprintf(path, sizeof(path), "%s/events.csv", directory);
    FILE *file = fopen(path, "r");
    assert(file != NULL);
    char line[512];
    unsigned starts = 0u, stops = 0u;
    while (fgets(line, sizeof(line), file) != NULL) {
        if (strstr(line, "INICIO_NODO") != NULL) ++starts;
        if (strstr(line, "PARADA_NODO") != NULL) ++stops;
    }
    assert(fclose(file) == 0 && starts == 2u && stops == (mode == 4u ? 1u : 2u));
    assert(unlink(path) == 0);
    snprintf(path, sizeof(path), "%s/report.html", directory);
    file = fopen(path, "r");
    assert(file != NULL);
    bool incomplete = false;
    while (fgets(line, sizeof(line), file) != NULL)
        if (strstr(line, "REPORTE INCOMPLETO") != NULL) incomplete = true;
    assert(fclose(file) == 0 && incomplete == (mode >= 3u));
    assert(unlink(path) == 0 && rmdir(directory) == 0);
}

int main(void)
{
    for (unsigned mode = 0u; mode < 5u; ++mode) test_session(mode);
    puts("OK: graceful report session tests");
    return 0;
}
