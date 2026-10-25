#define _DEFAULT_SOURCE

#include "host/event_queue.h"

#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <sys/mman.h>
#include <time.h>

/* Todos los datos de coordinación y los eventos residen en memoria compartida */
struct EventQueue {
    pthread_mutex_t mutex;
    pthread_cond_t readable;
    pthread_cond_t writable;
    atomic_bool stop_requested;
    atomic_bool incomplete;
    bool closed;
    bool failed;
    size_t head;
    size_t count;
    Event events[EVENT_QUEUE_CAPACITY];
};

/*
 * Recuperar el mutex si un proceso terminó dentro de la región crítica.
 * La cola se marca fallida porque no se puede asegurar la integridad del anillo.
 */
static int recover_lock(EventQueue *queue, int result)
{
    if (result == EOWNERDEAD) {
        queue->failed = true;
        queue->closed = true;
        atomic_store(&queue->stop_requested, true);
        atomic_store(&queue->incomplete, true);
        result = pthread_mutex_consistent(&queue->mutex);
        pthread_cond_broadcast(&queue->readable);
        pthread_cond_broadcast(&queue->writable);
    }

    return result;
}

/* Inicializar recursos con atributos explícitos para procesos distintos */
EventQueue *event_queue_create(void)
{
    EventQueue *queue = mmap(NULL, sizeof(*queue), PROT_READ | PROT_WRITE,
                            MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (queue == MAP_FAILED)
        return NULL;

    pthread_mutexattr_t mutex_attr;
    pthread_condattr_t cond_attr;
    bool have_mutex_attr = false, have_cond_attr = false;
    bool have_mutex = false, have_readable = false;

    if (pthread_mutexattr_init(&mutex_attr) != 0)
        goto fail;
    have_mutex_attr = true;
    if (pthread_condattr_init(&cond_attr) != 0)
        goto fail;
    have_cond_attr = true;

    if (pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED) != 0 ||
        pthread_mutexattr_setrobust(&mutex_attr, PTHREAD_MUTEX_ROBUST) != 0 ||
        pthread_condattr_setpshared(&cond_attr, PTHREAD_PROCESS_SHARED) != 0 ||
        pthread_condattr_setclock(&cond_attr, CLOCK_MONOTONIC) != 0)
        goto fail;

    if (pthread_mutex_init(&queue->mutex, &mutex_attr) != 0)
        goto fail;
    have_mutex = true;
    if (pthread_cond_init(&queue->readable, &cond_attr) != 0)
        goto fail;
    have_readable = true;
    if (pthread_cond_init(&queue->writable, &cond_attr) != 0)
        goto fail;

    pthread_mutexattr_destroy(&mutex_attr);
    pthread_condattr_destroy(&cond_attr);
    atomic_init(&queue->stop_requested, false);
    atomic_init(&queue->incomplete, false);
    return queue;

fail:
    if (have_readable) pthread_cond_destroy(&queue->readable);
    if (have_mutex) pthread_mutex_destroy(&queue->mutex);
    if (have_cond_attr) pthread_condattr_destroy(&cond_attr);
    if (have_mutex_attr) pthread_mutexattr_destroy(&mutex_attr);
    munmap(queue, sizeof(*queue));
    return NULL;
}

/* Usar un único vencimiento absoluto aunque haya despertares espurios */
static bool deadline_for(int timeout_ms, struct timespec *deadline)
{
    if (timeout_ms < -1)
        return false;
    if (timeout_ms == -1)
        return true;
    if (clock_gettime(CLOCK_MONOTONIC, deadline) != 0)
        return false;
    deadline->tv_sec += timeout_ms / 1000;
    deadline->tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
    if (deadline->tv_nsec >= 1000000000L) {
        ++deadline->tv_sec;
        deadline->tv_nsec -= 1000000000L;
    }
    return true;
}

static int wait_condition(EventQueue *queue, pthread_cond_t *condition,
                          int timeout_ms, const struct timespec *deadline)
{
    int result = timeout_ms == -1
        ? pthread_cond_wait(condition, &queue->mutex)
        : pthread_cond_timedwait(condition, &queue->mutex, deadline);
    return recover_lock(queue, result);
}

/* Publicar sin sobrescribir los eventos que todavía no consumió reportes */
EventQueueResult event_queue_push(EventQueue *queue, const Event *event, int timeout_ms)
{
    struct timespec deadline;
    if (queue == NULL || event == NULL || !deadline_for(timeout_ms, &deadline))
        return EVENT_QUEUE_ERROR;
    if (recover_lock(queue, pthread_mutex_lock(&queue->mutex)) != 0)
        return EVENT_QUEUE_ERROR;

    EventQueueResult result = EVENT_QUEUE_OK;
    while (!queue->closed && !queue->failed && queue->count == EVENT_QUEUE_CAPACITY) {
        int status = wait_condition(queue, &queue->writable, timeout_ms, &deadline);
        if (status != 0) {
            result = status == ETIMEDOUT ? EVENT_QUEUE_TIMEOUT : EVENT_QUEUE_ERROR;
            goto done;
        }
    }

    if (queue->failed) result = EVENT_QUEUE_ERROR;
    else if (queue->closed) result = EVENT_QUEUE_CLOSED;
    else {
        size_t tail = (queue->head + queue->count) % EVENT_QUEUE_CAPACITY;
        queue->events[tail] = *event;
        ++queue->count;
        pthread_cond_signal(&queue->readable);
    }

done:
    pthread_mutex_unlock(&queue->mutex);
    return result;
}

/* Consumir hasta que el propietario cierre la cola y se agoten los eventos */
EventQueueResult event_queue_pop(EventQueue *queue, Event *event, int timeout_ms)
{
    struct timespec deadline;
    if (queue == NULL || event == NULL || !deadline_for(timeout_ms, &deadline))
        return EVENT_QUEUE_ERROR;
    if (recover_lock(queue, pthread_mutex_lock(&queue->mutex)) != 0)
        return EVENT_QUEUE_ERROR;

    EventQueueResult result = EVENT_QUEUE_OK;
    while (!queue->closed && !queue->failed && queue->count == 0u) {
        int status = wait_condition(queue, &queue->readable, timeout_ms, &deadline);
        if (status != 0) {
            result = status == ETIMEDOUT ? EVENT_QUEUE_TIMEOUT : EVENT_QUEUE_ERROR;
            goto done;
        }
    }

    if (queue->failed) result = EVENT_QUEUE_ERROR;
    else if (queue->count == 0u) result = EVENT_QUEUE_CLOSED;
    else {
        *event = queue->events[queue->head];
        queue->head = (queue->head + 1u) % EVENT_QUEUE_CAPACITY;
        --queue->count;
        pthread_cond_signal(&queue->writable);
    }

done:
    pthread_mutex_unlock(&queue->mutex);
    return result;
}

void event_queue_request_stop(EventQueue *queue)
{
    if (queue != NULL)
        atomic_store(&queue->stop_requested, true);
}

bool event_queue_stop_requested(const EventQueue *queue)
{
    return queue == NULL || atomic_load(&queue->stop_requested);
}

void event_queue_mark_incomplete(EventQueue *queue)
{
    if (queue != NULL) {
        atomic_store(&queue->incomplete, true);
        event_queue_request_stop(queue);
    }
}

bool event_queue_is_incomplete(const EventQueue *queue)
{
    return queue == NULL || atomic_load(&queue->incomplete);
}

EventQueueResult event_queue_close(EventQueue *queue)
{
    if (queue == NULL || recover_lock(queue, pthread_mutex_lock(&queue->mutex)) != 0)
        return EVENT_QUEUE_ERROR;
    queue->closed = true;
    pthread_cond_broadcast(&queue->readable);
    pthread_cond_broadcast(&queue->writable);
    EventQueueResult result = queue->failed ? EVENT_QUEUE_ERROR : EVENT_QUEUE_OK;
    pthread_mutex_unlock(&queue->mutex);
    return result;
}

void event_queue_destroy(EventQueue *queue)
{
    if (queue == NULL)
        return;
    /* Un hijo terminado por señal puede dejar contadores internos de espera.
     * Tras waitpid no quedan usuarios; munmap libera también ese caso anormal. */
    if (!event_queue_is_incomplete(queue)) {
        pthread_cond_destroy(&queue->readable);
        pthread_cond_destroy(&queue->writable);
        pthread_mutex_destroy(&queue->mutex);
    }
    munmap(queue, sizeof(*queue));
}
