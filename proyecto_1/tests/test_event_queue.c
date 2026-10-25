#include <assert.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

#include "host/event_queue.h"

static void wait_ok(pid_t child)
{
    int status;
    assert(waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

/* Varios procesos, más eventos que espacios del anillo, sin pérdidas ni duplicados */
static void test_producers(void)
{
    EventQueue *queue = event_queue_create();
    assert(queue != NULL);
    pid_t children[3];
    for (unsigned producer = 0; producer < 3u; ++producer) {
        children[producer] = fork();
        assert(children[producer] >= 0);
        if (children[producer] == 0) {
            for (unsigned sequence = 0; sequence < 600u; ++sequence) {
                Event event = {.type = EVENT_TICK_REPORT, .node_id = producer,
                    .job_id = sequence, .tick = sequence};
                assert(event_queue_push(queue, &event, -1) == EVENT_QUEUE_OK);
            }
            _exit(0);
        }
    }
    unsigned received[3] = {0};
    for (unsigned i = 0; i < 1800u; ++i) {
        Event event;
        assert(event_queue_pop(queue, &event, 2000) == EVENT_QUEUE_OK);
        assert(event.node_id < 3u);
        assert(event.job_id == received[event.node_id]++);
    }
    for (unsigned i = 0; i < 3u; ++i) {
        wait_ok(children[i]);
        assert(received[i] == 600u);
    }
    assert(event_queue_close(queue) == EVENT_QUEUE_OK);
    Event event;
    assert(event_queue_pop(queue, &event, 0) == EVENT_QUEUE_CLOSED);
    event_queue_destroy(queue);
}

/* Cerrar despierta consumidores y productores, conservando los eventos pendientes */
static void test_close(bool full)
{
    EventQueue *queue = event_queue_create();
    assert(queue != NULL);
    Event event = {.type = EVENT_TICK_REPORT};
    if (full)
        for (unsigned i = 0; i < EVENT_QUEUE_CAPACITY; ++i)
            assert(event_queue_push(queue, &event, 0) == EVENT_QUEUE_OK);
    if (full) assert(event_queue_push(queue, &event, 0) == EVENT_QUEUE_TIMEOUT);

    int ready[2];
    assert(pipe(ready) == 0);
    pid_t child = fork();
    assert(child >= 0);
    if (child == 0) {
        close(ready[0]);
        assert(write(ready[1], "R", 1u) == 1);
        close(ready[1]);
        EventQueueResult result = full ? event_queue_push(queue, &event, -1)
                                       : event_queue_pop(queue, &event, -1);
        assert(result == EVENT_QUEUE_CLOSED);
        _exit(0);
    }
    close(ready[1]);
    char value;
    assert(read(ready[0], &value, 1u) == 1);
    close(ready[0]);
    assert(event_queue_close(queue) == EVENT_QUEUE_OK);
    wait_ok(child);
    if (full)
        for (unsigned i = 0; i < EVENT_QUEUE_CAPACITY; ++i)
            assert(event_queue_pop(queue, &event, 0) == EVENT_QUEUE_OK);
    assert(event_queue_pop(queue, &event, 0) == EVENT_QUEUE_CLOSED);
    assert(event_queue_push(queue, &event, 0) == EVENT_QUEUE_CLOSED);
    event_queue_destroy(queue);
}

int main(void)
{
    test_producers();
    test_close(false);
    test_close(true);

    EventQueue *queue = event_queue_create();
    assert(queue != NULL);
    Event event = {.type = EVENT_NODE_STOPPED};
    assert(event_queue_pop(queue, &event, 0) == EVENT_QUEUE_TIMEOUT);
    assert(event_queue_push(queue, &event, -2) == EVENT_QUEUE_ERROR);
    assert(!event_queue_stop_requested(queue));
    event_queue_request_stop(queue);
    assert(event_queue_stop_requested(queue));
    assert(event_queue_push(queue, &event, 0) == EVENT_QUEUE_OK);
    assert(event_queue_close(queue) == EVENT_QUEUE_OK);
    assert(event_queue_pop(queue, &event, 0) == EVENT_QUEUE_OK);
    assert(event.type == EVENT_NODE_STOPPED);
    event_queue_destroy(queue);
    puts("OK: shared event queue tests");
    return 0;
}
