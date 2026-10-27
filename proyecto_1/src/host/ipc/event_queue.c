#define _DEFAULT_SOURCE

#include "host/event_queue.h"

#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define EVENT_QUEUE_SHM_NAME_SIZE 64u

/* La cola y sus semáforos viven en el objeto POSIX compartido entre procesos. */
struct EventQueue {
    sem_t mutex;
    sem_t readable;
    sem_t writable;
    atomic_bool stop_requested;
    atomic_bool incomplete;
    atomic_bool failed;
    bool closed;
    size_t head;
    size_t count;
    char shm_name[EVENT_QUEUE_SHM_NAME_SIZE];
    Event events[EVENT_QUEUE_CAPACITY];
};

static unsigned long long queue_sequence;

/**
 * @brief Calcula el límite absoluto que usarán las esperas con timeout.
 * @return false si el timeout no es válido o falla la lectura del reloj.
 */
static bool deadline_for(int timeout_ms, struct timespec *deadline)
{
    if (timeout_ms < -1)
        return false;
    if (timeout_ms == -1)
        return true;
    if (clock_gettime(CLOCK_REALTIME, deadline) != 0)
        return false;
    deadline->tv_sec += timeout_ms / 1000;
    deadline->tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
    if (deadline->tv_nsec >= 1000000000L) {
        ++deadline->tv_sec;
        deadline->tv_nsec -= 1000000000L;
    }
    return true;
}

/**
 * @brief Espera un semáforo sin espera, con timeout o indefinidamente.
 * @return Cero si lo adquirió; ETIMEDOUT si venció el plazo; errno ante error.
 */
static int semaphore_wait(sem_t *semaphore, int timeout_ms,
                          const struct timespec *deadline)
{
    int result;
    do {
        if (timeout_ms == -1)
            result = sem_wait(semaphore);
        else if (timeout_ms == 0)
            result = sem_trywait(semaphore);
        else
            result = sem_timedwait(semaphore, deadline);
    } while (result != 0 && errno == EINTR);

    if (result == 0)
        return 0;
    if ((timeout_ms == 0 && errno == EAGAIN) ||
        (timeout_ms > 0 && errno == ETIMEDOUT))
        return ETIMEDOUT;
    return errno;
}

/** @brief Despierta lectores y escritores bloqueados durante el cierre/error. */
static void wake_waiters(EventQueue *queue)
{
    (void)sem_post(&queue->readable);
    for (size_t i = 0u; i < EVENT_QUEUE_CAPACITY; ++i)
        (void)sem_post(&queue->writable);
}

/** @brief Marca la cola como fallida/incompleta y libera a quienes esperan. */
static void mark_failed(EventQueue *queue)
{
    atomic_store(&queue->failed, true);
    atomic_store(&queue->incomplete, true);
    atomic_store(&queue->stop_requested, true);
    wake_waiters(queue);
}

/** @brief Construye un nombre POSIX único para la memoria compartida de la cola. */
static bool make_shared_name(char *name, size_t capacity)
{
    int written = snprintf(name, capacity, "/so_event_queue_%ld_%llu",
                           (long)getpid(), queue_sequence++);
    return written > 0 && (size_t)written < capacity;
}

/**
 * @brief Crea la cola en memoria compartida y sus semáforos entre procesos.
 * @return Puntero compartido inicializado o NULL si falla la preparación.
 */
EventQueue *event_queue_create(void)
{
    char name[EVENT_QUEUE_SHM_NAME_SIZE];
    if (!make_shared_name(name, sizeof(name)))
        return NULL;

    int descriptor = shm_open(name, O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR);
    if (descriptor < 0)
        return NULL;

    if (ftruncate(descriptor, (off_t)sizeof(EventQueue)) != 0) {
        close(descriptor);
        shm_unlink(name);
        return NULL;
    }

    /* MAP_SHARED hace que los cambios sean visibles para los procesos hijos. */
    EventQueue *queue = mmap(NULL, sizeof(*queue), PROT_READ | PROT_WRITE,
                             MAP_SHARED, descriptor, 0);
    int map_errno = errno;
    close(descriptor);
    if (queue == MAP_FAILED) {
        shm_unlink(name);
        errno = map_errno;
        return NULL;
    }

    memset(queue, 0, sizeof(*queue));
    memcpy(queue->shm_name, name, strlen(name) + 1u);
    bool mutex_ready = false;
    bool readable_ready = false;
    bool writable_ready = false;
    /* pshared=1 habilita sincronización entre procesos, no solo entre hilos. */
    if (sem_init(&queue->mutex, 1, 1u) != 0)
        goto fail;
    mutex_ready = true;
    if (sem_init(&queue->readable, 1, 0u) != 0)
        goto fail;
    readable_ready = true;
    if (sem_init(&queue->writable, 1, EVENT_QUEUE_CAPACITY) != 0)
        goto fail;
    writable_ready = true;

    atomic_init(&queue->stop_requested, false);
    atomic_init(&queue->incomplete, false);
    atomic_init(&queue->failed, false);
    queue->closed = false;
    queue->head = 0u;
    queue->count = 0u;
    return queue;

fail:
    if (writable_ready) sem_destroy(&queue->writable);
    if (readable_ready) sem_destroy(&queue->readable);
    if (mutex_ready) sem_destroy(&queue->mutex);
    munmap(queue, sizeof(*queue));
    shm_unlink(name);
    return NULL;
}

/**
 * @brief Inserta una copia del evento al final de la cola acotada.
 * @return OK al insertar, TIMEOUT si no hay espacio, u otro estado ante cierre/error.
 */
EventQueueResult event_queue_push(EventQueue *queue, const Event *event, int timeout_ms)
{
    struct timespec deadline;
    if (queue == NULL || event == NULL ||
        !deadline_for(timeout_ms, &deadline))
        return EVENT_QUEUE_ERROR;

    /* Reservar espacio antes de tomar el mutex evita bloquear a otros productores. */
    int status = semaphore_wait(&queue->writable, timeout_ms, &deadline);
    if (status == ETIMEDOUT)
        return EVENT_QUEUE_TIMEOUT;
    if (status != 0) {
        mark_failed(queue);
        return EVENT_QUEUE_ERROR;
    }

    status = semaphore_wait(&queue->mutex, -1, NULL);
    if (status != 0) {
        (void)sem_post(&queue->writable);
        mark_failed(queue);
        return EVENT_QUEUE_ERROR;
    }

    EventQueueResult result = EVENT_QUEUE_OK;
    if (atomic_load(&queue->failed))
        result = EVENT_QUEUE_ERROR;
    else if (queue->closed)
        result = EVENT_QUEUE_CLOSED;
    else {
        /* Calcular el final circular y copiar el evento como una unidad. */
        size_t tail = (queue->head + queue->count) % EVENT_QUEUE_CAPACITY;
        queue->events[tail] = *event;
        ++queue->count;
    }

    (void)sem_post(&queue->mutex);
    if (result != EVENT_QUEUE_OK) {
        (void)sem_post(&queue->writable);
        return result;
    }
    if (sem_post(&queue->readable) != 0) {
        mark_failed(queue);
        return EVENT_QUEUE_ERROR;
    }
    return EVENT_QUEUE_OK;
}

/**
 * @brief Extrae el evento más antiguo y libera un espacio para productores.
 * @return OK con evento, TIMEOUT si no llegó uno, CLOSED tras drenar al cerrar.
 */
EventQueueResult event_queue_pop(EventQueue *queue, Event *event, int timeout_ms)
{
    struct timespec deadline;
    if (queue == NULL || event == NULL ||
        !deadline_for(timeout_ms, &deadline))
        return EVENT_QUEUE_ERROR;

    /* Esperar que exista un evento; después el mutex protege índices y contador. */
    int status = semaphore_wait(&queue->readable, timeout_ms, &deadline);
    if (status == ETIMEDOUT) {
        status = semaphore_wait(&queue->mutex, -1, NULL);
        if (status != 0) {
            mark_failed(queue);
            return EVENT_QUEUE_ERROR;
        }
        EventQueueResult result = EVENT_QUEUE_TIMEOUT;
        if (atomic_load(&queue->failed))
            result = EVENT_QUEUE_ERROR;
        else if (queue->closed && queue->count == 0u)
            result = EVENT_QUEUE_CLOSED;
        if (sem_post(&queue->mutex) != 0) {
            mark_failed(queue);
            return EVENT_QUEUE_ERROR;
        }
        return result;
    }
    if (status != 0) {
        mark_failed(queue);
        return EVENT_QUEUE_ERROR;
    }

    status = semaphore_wait(&queue->mutex, -1, NULL);
    if (status != 0) {
        mark_failed(queue);
        return EVENT_QUEUE_ERROR;
    }

    EventQueueResult result = EVENT_QUEUE_OK;
    if (atomic_load(&queue->failed))
        result = EVENT_QUEUE_ERROR;
    else if (queue->count == 0u)
        result = queue->closed ? EVENT_QUEUE_CLOSED : EVENT_QUEUE_ERROR;
    else {
        /* Retirar por FIFO y avanzar el índice circular de lectura. */
        *event = queue->events[queue->head];
        queue->head = (queue->head + 1u) % EVENT_QUEUE_CAPACITY;
        --queue->count;
    }

    (void)sem_post(&queue->mutex);
    if (result == EVENT_QUEUE_OK) {
        if (sem_post(&queue->writable) != 0) {
            mark_failed(queue);
            return EVENT_QUEUE_ERROR;
        }
    } else if (result == EVENT_QUEUE_ERROR) {
        mark_failed(queue);
    }
    return result;
}

/** @brief Solicita que los workers salgan de sus bucles de trabajo. */
void event_queue_request_stop(EventQueue *queue)
{
    /* Los productores consultan esta bandera para salir de sus bucles. */
    if (queue != NULL)
        atomic_store(&queue->stop_requested, true);
}

/** @brief Consulta si se solicitó terminar la sesión de productores. */
bool event_queue_stop_requested(const EventQueue *queue)
{
    return queue == NULL || atomic_load(&queue->stop_requested);
}

/** @brief Marca el resultado de sesión como incompleto y solicita el cierre. */
void event_queue_mark_incomplete(EventQueue *queue)
{
    /* El cierre por error también debe reflejarse en el resultado del reporte. */
    if (queue != NULL) {
        atomic_store(&queue->incomplete, true);
        event_queue_request_stop(queue);
    }
}

/** @brief Consulta si algún proceso reportó pérdida o fallo durante la sesión. */
bool event_queue_is_incomplete(const EventQueue *queue)
{
    return queue == NULL || atomic_load(&queue->incomplete);
}

/**
 * @brief Impide nuevas inserciones y despierta lectores para drenar o terminar.
 * Los eventos ya encolados permanecen disponibles para event_queue_pop.
 */
EventQueueResult event_queue_close(EventQueue *queue)
{
    if (queue == NULL)
        return EVENT_QUEUE_ERROR;

    int status = semaphore_wait(&queue->mutex, -1, NULL);
    if (status != 0) {
        mark_failed(queue);
        return EVENT_QUEUE_ERROR;
    }

    bool already_closed = queue->closed;
    queue->closed = true;
    bool failed = atomic_load(&queue->failed);
    (void)sem_post(&queue->mutex);

    if (!already_closed)
        wake_waiters(queue);
    return failed ? EVENT_QUEUE_ERROR : EVENT_QUEUE_OK;
}

/** @brief Destruye semáforos, mapeo y nombre POSIX de la cola compartida. */
void event_queue_destroy(EventQueue *queue)
{
    if (queue == NULL)
        return;
    char name[EVENT_QUEUE_SHM_NAME_SIZE];
    memcpy(name, queue->shm_name, sizeof(name));
    sem_destroy(&queue->readable);
    sem_destroy(&queue->writable);
    sem_destroy(&queue->mutex);
    munmap(queue, sizeof(*queue));
    shm_unlink(name);
}
