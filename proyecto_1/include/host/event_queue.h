#ifndef HOST_EVENT_QUEUE_H
#define HOST_EVENT_QUEUE_H

#include <stdbool.h>

#include "common/events.h"

/* Capacidad acotada: al llenarse se aplica contrapresión, no se descartan eventos */
#define EVENT_QUEUE_CAPACITY 256u

typedef struct EventQueue EventQueue;

typedef enum {
    EVENT_QUEUE_OK = 0,
    EVENT_QUEUE_CLOSED,
    EVENT_QUEUE_TIMEOUT,
    EVENT_QUEUE_ERROR
} EventQueueResult;

/*
 * Crea una cola en mmap compartido, para heredarla mediante fork().
 * Usa mutex y condiciones POSIX compartidos entre procesos, sin espera activa.
 * Retorna NULL si no se pueden reservar o inicializar los recursos.
 */
EventQueue *event_queue_create(void);

/*
 * Agrega/extrae una copia de Event. timeout_ms = -1 espera sin límite.
 * Con timeout_ms = 0 solo se espera si el recurso ya está disponible.
 * push admite varios productores; pop tiene un único consumidor de reportes.
 */
EventQueueResult event_queue_push(EventQueue *queue, const Event *event, int timeout_ms);
EventQueueResult event_queue_pop(EventQueue *queue, Event *event, int timeout_ms);

/* Solicita detener productores; permite seguir publicando sus eventos finales */
void event_queue_request_stop(EventQueue *queue);
bool event_queue_stop_requested(const EventQueue *queue);

/* Registrar una terminación anormal para identificar el reporte como incompleto */
void event_queue_mark_incomplete(EventQueue *queue);
bool event_queue_is_incomplete(const EventQueue *queue);

/* Impide nuevos push; pop drena lo pendiente y luego retorna CLOSED */
EventQueueResult event_queue_close(EventQueue *queue);

/* Solo el propietario, después de esperar a todos los hijos con waitpid() */
void event_queue_destroy(EventQueue *queue);

#endif // HOST_EVENT_QUEUE_H
