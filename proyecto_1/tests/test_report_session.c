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
    bool read_input;
} TestWorker;

typedef struct {
    int markers;
    size_t index;
} CoordinatedWorker;

typedef struct {
    int markers;
} CoordinatedSession;

typedef struct {
    char role;
    pid_t pid;
} ProcessMarker;

static void record_process(int fd, char role)
{
    ProcessMarker marker = {.role = role, .pid = getpid()};
    assert(write(fd, &marker, sizeof(marker)) == (ssize_t)sizeof(marker));
}

static int run_coordinated_worker(EventQueue *queue, void *context)
{
    CoordinatedWorker *worker = context;
    record_process(worker->markers, (char)('0' + worker->index));
    while (!event_queue_stop_requested(queue)) {
        struct timespec pause = {.tv_nsec = 1000000L};
        nanosleep(&pause, NULL);
    }
    return 0;
}

static int stop_coordinated_worker(EventQueue *queue, void *context)
{
    (void)queue;
    (void)context;
    return 0;
}

static int coordinate_test_session(EventQueue *queue, void *context)
{
    CoordinatedSession *session = context;
    record_process(session->markers, 'C');
    event_queue_request_stop(queue);
    return 0;
}

static int setup_test_child(bool reporter, size_t worker_index, void *context)
{
    (void)worker_index;
    CoordinatedSession *session = context;
    if (reporter)
        record_process(session->markers, 'R');
    return 0;
}

static int setup_test_parent(void *context)
{
    CoordinatedSession *session = context;
    record_process(session->markers, 'P');
    return 0;
}

static int run_worker(EventQueue *queue, void *context)
{
    TestWorker *worker = context;
    Event event = {.type = EVENT_NODE_STARTED, .node_id = worker->id};
    assert(event_queue_push(queue, &event, 1000) == EVENT_QUEUE_OK);
    assert(write(worker->markers, "R", 1u) == 1);
    if (worker->fail) return 1;
    if (worker->read_input) {
        char character;
        do {
            ssize_t length = read(STDIN_FILENO, &character, 1u);
            if (length <= 0) return 1;
        } while (character != '\n');
        event_queue_request_stop(queue);
        return 0;
    }
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
            {.id = 1u, .markers = markers[1], .read_input = mode == 5u},
            {.id = 2u, .markers = markers[1], .fail = mode == 3u, .hang = mode == 4u}
        };
        ReportWorker workers[] = {
            {.run = run_worker, .stop = stop_worker, .context = &contexts[0],
                .worker_reads_stdin = mode == 5u},
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
    if (mode == 5u) assert(write(input[1], "q\n", 2u) == 2);
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
    assert(WIFEXITED(status) &&
           WEXITSTATUS(status) == (mode == 3u || mode == 4u ? 1 : 0));

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
    assert(fclose(file) == 0 && incomplete == (mode == 3u || mode == 4u));
    assert(unlink(path) == 0 && rmdir(directory) == 0);
}

static void test_coordinated_session(void)
{
    char directory[] = "/tmp/so_coordinated_session_test_XXXXXX";
    assert(mkdtemp(directory) != NULL);
    int markers[2];
    assert(pipe(markers) == 0);

    CoordinatedSession session = {.markers = markers[1]};
    CoordinatedWorker contexts[] = {
        {.markers = markers[1], .index = 0u},
        {.markers = markers[1], .index = 1u}
    };
    ReportWorker workers[] = {
        {.run = run_coordinated_worker, .stop = stop_coordinated_worker,
            .context = &contexts[0]},
        {.run = run_coordinated_worker, .stop = stop_coordinated_worker,
            .context = &contexts[1]}
    };
    pid_t parent = getpid();
    int result = report_session_run_coordinated(
        directory, workers, 2u, coordinate_test_session,
        setup_test_child, setup_test_parent, &session
    );
    close(markers[1]);
    assert(result == 0);

    ProcessMarker observed[5];
    size_t count = 0u;
    ssize_t length;
    while ((length = read(markers[0], &observed[count], sizeof(observed[count]))) > 0) {
        assert(length == (ssize_t)sizeof(observed[count]));
        ++count;
        assert(count <= sizeof(observed) / sizeof(observed[0]));
    }
    assert(length == 0);
    close(markers[0]);
    assert(count == 5u);

    pid_t reporter = 0, first_worker = 0, second_worker = 0;
    bool coordinator_seen = false, parent_setup_seen = false;
    for (size_t i = 0u; i < count; ++i) {
        switch (observed[i].role) {
        case 'R':
            reporter = observed[i].pid;
            break;
        case '0':
            first_worker = observed[i].pid;
            break;
        case '1':
            second_worker = observed[i].pid;
            break;
        case 'C':
            assert(observed[i].pid == parent);
            coordinator_seen = true;
            break;
        case 'P':
            assert(observed[i].pid == parent);
            parent_setup_seen = true;
            break;
        default:
            assert(0);
        }
    }
    assert(coordinator_seen && parent_setup_seen && reporter > 0 &&
           first_worker > 0 && second_worker > 0);
    assert(reporter != parent && first_worker != parent && second_worker != parent);
    assert(reporter != first_worker && reporter != second_worker &&
           first_worker != second_worker);

    char path[256];
    snprintf(path, sizeof(path), "%s/events.csv", directory);
    assert(unlink(path) == 0);
    snprintf(path, sizeof(path), "%s/report.html", directory);
    assert(unlink(path) == 0);
    assert(rmdir(directory) == 0);
}

int main(void)
{
    for (unsigned mode = 0u; mode < 6u; ++mode) test_session(mode);
    test_coordinated_session();
    puts("OK: graceful report session tests");
    return 0;
}
