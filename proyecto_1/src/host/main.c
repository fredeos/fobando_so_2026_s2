#define _DEFAULT_SOURCE

#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>
#include "host/report_session.h"
#include "host/event_queue.h"

#include "common/message_parser.h"
#include "host/config.h"
#include "host/statistics.h"
#include "host/task_parser.h"
#include "comms/comms.h"
#include "timing/timing.h"

#define BUFFER_LEN 80      // message buffer len
#define EXPECTED_DEVICES 2 // expected devices

#define TICK_SIZE   10  // 10 ms
#define TICK_LIMIT  10  // 10 ticks

#define LIFETIME   30u // program lifetime: 30 s
#define MSG_TICK   1u  // send message every 1 s
#define PRIORITY    1  // priority level 1

#define HANDSHAKE_TIMEOUT_SECONDS 5
#define CALIBRATION_REQUEST_ID    2u
#define FIRST_DEPLOY_REQUEST_ID   3u
#define FIRST_CONTROL_REQUEST_ID  0x80000000u

typedef struct {
    HostConfig config;
    const char *devices[EXPECTED_DEVICES];
    uint32_t node_ids[EXPECTED_DEVICES];
    comms_handler_t handlers[EXPECTED_DEVICES];
    char sent[EXPECTED_DEVICES][COMMS_MESSAGE_CAPACITY];
    char received[EXPECTED_DEVICES][COMMS_MESSAGE_CAPACITY];
    bool initialized[EXPECTED_DEVICES];
    bool handler_started[EXPECTED_DEVICES];
    bool running[EXPECTED_DEVICES];
    size_t initialized_count;
} SerialReportSession;

static uint64_t latest_node_ticks[EXPECTED_DEVICES];
static bool node_started_once[EXPECTED_DEVICES];
static bool node_reports_drained[EXPECTED_DEVICES];
static bool node_stop_pending[EXPECTED_DEVICES];

static bool drain_node_reports(
    EventQueue *queue,
    const uint32_t node_ids[EXPECTED_DEVICES],
    comms_handler_t handlers[EXPECTED_DEVICES],
    const bool required[EXPECTED_DEVICES]
);

/**
 * @brief Publica un evento en la cola compartida de reportes.
 *
 * Reintenta mientras la cola esté temporalmente llena y marca la sesión como
 * incompleta si no puede publicar el evento.
 *
 * @param queue Cola de eventos de la sesión.
 * @param event Evento que se agregará al reporte.
 *
 * @return true si el evento se publicó correctamente.
 */
static bool publish_report_event(EventQueue *queue, const Event *event)
{
    EventQueueResult result;
    do {
        result = event_queue_push(queue, event, 200);
    } while (result == EVENT_QUEUE_TIMEOUT);
    if (result != EVENT_QUEUE_OK) {
        fprintf(stderr, "No se pudo publicar el evento en la cola de reportes (%d).\n",
            (int)result);
        event_queue_mark_incomplete(queue);
        return false;
    }
    return true;
}

/**
 * @brief Actualiza el último tick conocido de un nodo a partir de un evento.
 *
 * Para eventos de ejecución e inactividad considera también la duración del
 * intervalo reportado.
 *
 * @param event Evento recibido desde el nodo.
 */
static void record_node_event_tick(const Event *event)
{
    size_t index = event->node_id == 1u ? 0u : event->node_id == 2u ? 1u :
        EXPECTED_DEVICES;
    if (index >= EXPECTED_DEVICES)
        return;
    uint64_t end = event->tick;
    if ((event->type == EVENT_EXECUTION_SLICE ||
         event->type == EVENT_IDLE_SLICE) &&
        UINT64_MAX - end >= event->duration_ticks)
        end += event->duration_ticks;
    if (end > latest_node_ticks[index])
        latest_node_ticks[index] = end;
}

/**
 * @brief Obtiene el tiempo monotónico actual en nanosegundos.
 *
 * @return Tiempo en nanosegundos, o cero si falla la lectura del reloj.
 */
static uint64_t host_time_ns(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        perror("clock_gettime");
        return 0u;
    }
    return (uint64_t)now.tv_sec * UINT64_C(1000000000) +
        (uint64_t)now.tv_nsec;
}

/**
 * @brief Obtiene el tiempo monotónico actual en milisegundos.
 *
 * @return Tiempo en milisegundos, o cero si falla la lectura del reloj.
 */
static uint64_t monotonic_time_ms(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        perror("clock_gettime");
        return 0u;
    }
    return (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
}

/**
 * @brief Serializa y prepara el envío de un mensaje a un nodo.
 *
 * @param handler Enlace serial del nodo destino.
 * @param buffer Buffer donde se serializará el mensaje.
 * @param message Mensaje que se enviará.
 * @param node_id Identificador del nodo para diagnóstico.
 * @param message_name Nombre del mensaje para diagnóstico.
 *
 * @return true si el mensaje se serializó y quedó listo para enviarse.
 */
static bool send_message(
    comms_handler_t *handler,
    char *buffer,
    const Message *message,
    uint32_t node_id,
    const char *message_name
)
{
    size_t json_length = 0u;
    MessageSerializeResult serialized = message_serialize(
        message, buffer, COMMS_MESSAGE_CAPACITY, &json_length
    );
    if (serialized != MESSAGE_SERIALIZE_OK) {
        fprintf(stderr, "No se pudo serializar %s para nodo %u (error %d).\n",
            message_name, node_id, (int)serialized);
        return false;
    }

    comms_status_t status = comms_request_send(handler, buffer, 0u);
    if (status != COMMS_OK) {
        fprintf(stderr, "No se pudo preparar %s para nodo %u (estado %d).\n",
            message_name, node_id, (int)status);
        return false;
    }
    printf("Enviando %s a nodo %u (%zu bytes JSON).\n",
        message_name, node_id, json_length);
    return true;
}

/**
 * @brief Espera el ACK de CONFIG correspondiente a una solicitud.
 *
 * Consume las respuestas recibidas por el hilo de comunicación y verifica
 * tipo, nodo y request_id antes de aceptar el ACK.
 *
 * @param handler   Enlace serial del nodo.
 * @param node_id   Identificador esperado en la respuesta.
 * @param request_id Identificador de la solicitud CONFIG.
 * @param device    Nombre del dispositivo para los mensajes de diagnóstico.
 *
 * @return true si llega el ACK esperado antes del timeout.
 */
static bool wait_for_ack(
    comms_handler_t *handler,
    uint32_t node_id,
    uint32_t request_id,
    const char *device
)
{
    time_t start = time(NULL);
    for (;;) {
        char response[COMMS_MESSAGE_CAPACITY];
        size_t response_size = 0u;
        bool ready;

        pthread_mutex_lock(&handler->mutex);
        ready = handler->recv.rdy;
        if (ready) {
            response_size = handler->recv.size;
            memcpy(response, handler->recv.content, response_size);
            response[response_size] = '\0';
            handler->recv.rdy = false;
        }
        pthread_mutex_unlock(&handler->mutex);

        if (ready) {
            if (response_size > 0u && response[response_size - 1u] == '\0')
                --response_size;

            Message reply;
            MessageParseResult parsed = message_parse(
                response, response_size, &reply
            );
            /* Acepta solo el ACK correlacionado con la petición CONFIG enviada. */
            if (parsed == MESSAGE_PARSE_OK &&
                reply.type == MSG_ACK &&
                reply.node_id == node_id &&
                reply.request_id == request_id &&
                reply.data.reply.type == MSG_CONFIG) {
            comms_acquire_send(handler);
            printf("CONFIG confirmado por nodo %u (%s).\n", node_id, device);
            return true;
            }

            if (parsed != MESSAGE_PARSE_OK && response_size > 0u &&
            response[0] == '{') {
            fprintf(stderr, "Respuesta no JSON válida de nodo %u (%s): %s\n",
                node_id, device, message_parse_result_string(parsed));
            } else if (parsed == MESSAGE_PARSE_OK) {
            fprintf(stderr, "Respuesta inesperada de nodo %u (%s): tipo %d.\n",
                node_id, device, (int)reply.type);
            }
        }

        if (time(NULL) - start >= HANDSHAKE_TIMEOUT_SECONDS) {
            fprintf(stderr, "Timeout esperando ACK de nodo %u en %s.\n",
                node_id, device);
            return false;
        }
        struct timespec pause = {.tv_nsec = 10000000L};
        nanosleep(&pause, NULL);
    }
}

/**
 * @brief Espera el resultado de calibración del nodo.
 *
 * Acepta una respuesta CALIBRATE_RESPONSE con capacidad positiva o termina
 * con error si recibe un NACK correlacionado o vence el timeout.
 *
 * @param handler     Enlace serial del nodo.
 * @param node_id     Identificador esperado en la respuesta.
 * @param request_id  Identificador de CALIBRATE_REQUEST.
 * @param device      Nombre del dispositivo para los mensajes de diagnóstico.
 * @param ut_per_tick Dirección donde se guardará la capacidad calibrada.
 *
 * @return true si se recibió una calibración válida.
 */
static bool wait_for_calibration(
    comms_handler_t *handler,
    uint32_t node_id,
    uint32_t request_id,
    const char *device,
    uint32_t *ut_per_tick
)
{
    time_t start = time(NULL);
    for (;;) {
        char response[COMMS_MESSAGE_CAPACITY];
        size_t response_size = 0u;
        bool ready;

        pthread_mutex_lock(&handler->mutex);
        ready = handler->recv.rdy;
        if (ready) {
            response_size = handler->recv.size;
            memcpy(response, handler->recv.content, response_size);
            response[response_size] = '\0';
            handler->recv.rdy = false;
        }
        pthread_mutex_unlock(&handler->mutex);

        if (ready) {
            if (response_size > 0u && response[response_size - 1u] == '\0')
                --response_size;

            Message reply;
            MessageParseResult parsed = message_parse(
                response, response_size, &reply
            );
            if (parsed == MESSAGE_PARSE_OK &&
                reply.node_id == node_id &&
                reply.request_id == request_id) {
                if (reply.type == MSG_CALIBRATE_RESPONSE &&
                    reply.data.ut_per_tick > 0u) {
                    *ut_per_tick = reply.data.ut_per_tick;
                    comms_acquire_send(handler);
                    printf("Calibración nodo %u (%s): %" PRIu32 " UT/tick.\n",
                        node_id, device, *ut_per_tick);
                    return true;
                }
                if (reply.type == MSG_NACK &&
                    reply.data.reply.type == MSG_CALIBRATE_REQUEST) {
                    fprintf(stderr,
                        "El nodo %u (%s) rechazó la calibración (razón %d).\n",
                        node_id, device, (int)reply.data.reply.reason);
                    return false;
                }
            } else if (parsed != MESSAGE_PARSE_OK && response_size > 0u &&
                       response[0] == '{') {
                fprintf(stderr, "Respuesta de calibración no válida de nodo %u (%s): %s\n",
                    node_id, device, message_parse_result_string(parsed));
            }
        }

        if (time(NULL) - start >= HANDSHAKE_TIMEOUT_SECONDS) {
            fprintf(stderr, "Timeout esperando calibración de nodo %u en %s.\n",
                node_id, device);
            return false;
        }
        struct timespec pause = {.tv_nsec = 10000000L};
        nanosleep(&pause, NULL);
    }
}

/**
 * @brief Espera el ACK o NACK del despliegue de una tarea.
 *
 * Solo acepta una respuesta cuyo nodo, request_id y tipo de solicitud
 * coincidan con el DEPLOY_TASK que está pendiente.
 *
 * @param handler   Enlace serial del nodo destino.
 * @param node_id   Identificador esperado en la respuesta.
 * @param request_id Identificador del despliegue.
 * @param device    Nombre del dispositivo para los mensajes de diagnóstico.
 * @param accepted  Recibe true para ACK y false para NACK.
 * @param reason    Recibe el motivo de rechazo; REJECT_NONE en caso de ACK.
 *
 * @return true si llegó una respuesta correlacionada antes del timeout.
 */
static bool wait_for_task_deploy(
    comms_handler_t *handler,
    uint32_t node_id,
    uint32_t request_id,
    const char *device,
    bool *accepted,
    RejectReason *reason
)
{
    /* Solo una respuesta ACK/NACK con nodo, request_id y tipo coincidentes cierra la petición. */
    time_t start = time(NULL);
    for (;;) {
        char response[COMMS_MESSAGE_CAPACITY];
        size_t response_size = 0u;
        bool ready;

        pthread_mutex_lock(&handler->mutex);
        ready = handler->recv.rdy;
        if (ready) {
            response_size = handler->recv.size;
            memcpy(response, handler->recv.content, response_size);
            response[response_size] = '\0';
            handler->recv.rdy = false;
        }
        pthread_mutex_unlock(&handler->mutex);

        if (ready) {
            if (response_size > 0u && response[response_size - 1u] == '\0')
                --response_size;

            Message reply;
            MessageParseResult parsed = message_parse(
                response, response_size, &reply
            );
            if (parsed == MESSAGE_PARSE_OK &&
                reply.node_id == node_id &&
                reply.request_id == request_id &&
                (reply.type == MSG_ACK || reply.type == MSG_NACK) &&
                reply.data.reply.type == MSG_DEPLOY_TASK) {
                *accepted = reply.type == MSG_ACK;
                *reason = reply.type == MSG_NACK
                    ? reply.data.reply.reason : REJECT_NONE;
                comms_acquire_send(handler);
                return true;
            }

            if (parsed != MESSAGE_PARSE_OK && response_size > 0u &&
                response[0] == '{') {
                fprintf(stderr,
                    "Respuesta de despliegue no válida de nodo %u (%s): %s\n",
                    node_id, device, message_parse_result_string(parsed));
            }
        }

        if (time(NULL) - start >= HANDSHAKE_TIMEOUT_SECONDS) {
            fprintf(stderr,
                "Timeout esperando ACK/NACK de despliegue del nodo %u (%s).\n",
                node_id, device);
            return false;
        }
        struct timespec pause = {.tv_nsec = 10000000L};
        nanosleep(&pause, NULL);
    }
}

/**
 * @brief Espera y valida la respuesta a START o STOP de un nodo.
 *
 * Correlaciona la respuesta por tipo, nodo y request_id. Informa el motivo
 * cuando el nodo rechaza el comando y libera el turno de envío del enlace.
 *
 * @param handler     Enlace serial del nodo.
 * @param node_id     Identificador esperado en la respuesta.
 * @param request_id  Identificador del comando de control.
 * @param request_type Tipo de comando enviado, START o STOP.
 * @param device      Nombre del dispositivo para los mensajes de diagnóstico.
 *
 * @return true si el nodo respondió ACK; false ante NACK, error o timeout.
 */
static bool wait_for_control_reply(
    EventQueue *queue,
    comms_handler_t *handler,
    uint32_t node_id,
    uint32_t request_id,
    MessageType request_type,
    const char *device,
    unsigned int timeout_seconds
)
{
    uint64_t start = monotonic_time_ms();
    for (;;) {
        char response[COMMS_MESSAGE_CAPACITY];
        size_t response_size = 0u;
        bool ready;

        pthread_mutex_lock(&handler->mutex);
        ready = handler->recv.rdy;
        if (ready) {
            response_size = handler->recv.size;
            memcpy(response, handler->recv.content, response_size);
            response[response_size] = '\0';
            handler->recv.rdy = false;
        }
        pthread_mutex_unlock(&handler->mutex);

        if (ready) {
            if (response_size > 0u && response[response_size - 1u] == '\0')
                --response_size;

            Message reply;
            MessageParseResult parsed = message_parse(
                response, response_size, &reply
            );
            if (parsed == MESSAGE_PARSE_OK &&
                reply.node_id == node_id &&
                reply.request_id == request_id &&
                (reply.type == MSG_ACK || reply.type == MSG_NACK) &&
                reply.data.reply.type == request_type) {
                comms_acquire_send(handler);
                if (reply.type == MSG_ACK) {
                    printf("%s confirmado por nodo %u (%s).\n",
                        request_type == MSG_START ? "START" : "STOP",
                        node_id, device);
                    return true;
                }

                fprintf(stderr,
                    "%s rechazado por nodo %u (%s): %s.\n",
                    request_type == MSG_START ? "START" : "STOP",
                    node_id, device,
                    report_reject_reason(reply.data.reply.reason));
                return false;
            }
            if (parsed == MESSAGE_PARSE_OK && reply.node_id == node_id &&
                (reply.type == MSG_EVENT || reply.type == MSG_TICK_REPORT)) {
                Event event = reply.type == MSG_EVENT
                    ? reply.data.event
                    : (Event){
                        .type = EVENT_TICK_REPORT,
                        .source = EVENT_SOURCE_NODE,
                        .node_id = reply.node_id,
                        .tick = reply.data.tick_report.tick,
                        .task_id = reply.data.tick_report.task_id
                    };
                event.host_time_ns = host_time_ns();
                record_node_event_tick(&event);
                size_t index = node_id == 1u ? 0u : node_id == 2u ? 1u :
                    EXPECTED_DEVICES;
                if (index < EXPECTED_DEVICES)
                    node_reports_drained[index] = false;
                if (!publish_report_event(queue, &event))
                    return false;
                continue;
            }

            if (parsed != MESSAGE_PARSE_OK && response_size > 0u &&
                response[0] == '{') {
                fprintf(stderr,
                    "Respuesta de control no válida de nodo %u (%s): %s\n",
                    node_id, device, message_parse_result_string(parsed));
            }
        }

        if (monotonic_time_ms() - start >= (uint64_t)timeout_seconds * 1000u) {
            fprintf(stderr, "Timeout esperando respuesta de %s en nodo %u (%s).\n",
                request_type == MSG_START ? "START" : "STOP", node_id, device);
            return false;
        }
        struct timespec pause = {.tv_nsec = 10000000L};
        nanosleep(&pause, NULL);
    }
}

/**
 * @brief Procesa los mensajes de reporte disponibles en los enlaces seriales.
 *
 * Convierte MSG_EVENT y MSG_TICK_REPORT en eventos de la sesión y detecta
 * PONG para confirmar que el nodo terminó de vaciar su cola de reportes.
 *
 * @param queue Cola donde se publicarán los eventos recibidos.
 * @param node_ids Identificadores de los nodos asociados a cada enlace.
 * @param handlers Enlaces seriales que se inspeccionarán.
 *
 * @return true si los mensajes disponibles se procesaron sin errores.
 */
static bool pump_node_reports(
    EventQueue *queue,
    const uint32_t node_ids[EXPECTED_DEVICES],
    comms_handler_t handlers[EXPECTED_DEVICES]
)
{
    bool success = true;
    for (size_t i = 0u; i < EXPECTED_DEVICES; ++i) {
        char response[COMMS_MESSAGE_CAPACITY];
        size_t response_size = 0u;

        pthread_mutex_lock(&handlers[i].mutex);
        bool ready = handlers[i].recv.rdy;
        if (ready) {
            response_size = handlers[i].recv.size;
            memcpy(response, handlers[i].recv.content, response_size);
            response[response_size] = '\0';
            handlers[i].recv.rdy = false;
        }
        pthread_mutex_unlock(&handlers[i].mutex);
        if (!ready)
            continue;

        if (response_size > 0u && response[response_size - 1u] == '\0')
            --response_size;

        Message message;
        MessageParseResult parsed = message_parse(response, response_size, &message);
        if (parsed != MESSAGE_PARSE_OK) {
            if (response_size >= 4u && memcmp(response, "PONG", 4u) == 0) {
                node_reports_drained[i] = true;
                if (node_stop_pending[i]) {
                    Event stopped = {
                        .type = EVENT_NODE_STOPPED,
                        .source = EVENT_SOURCE_HOST,
                        .node_id = node_ids[i],
                        .tick = latest_node_ticks[i],
                        .host_time_ns = host_time_ns()
                    };
                    if (publish_report_event(queue, &stopped))
                        node_stop_pending[i] = false;
                }
            }
            if (response_size > 0u && response[0] == '{') {
                fprintf(stderr, "Mensaje de reporte inválido del nodo %u: %s\n",
                    node_ids[i], message_parse_result_string(parsed));
                success = false;
            }
            continue;
        }
        if (message.node_id != node_ids[i]) {
            fprintf(stderr, "Mensaje recibido en enlace %u con node_id %u.\n",
                node_ids[i], message.node_id);
            success = false;
            continue;
        }

        Event event = {0};
        if (message.type == MSG_EVENT) {
            event = message.data.event;
        } else if (message.type == MSG_TICK_REPORT) {
            event.type = EVENT_TICK_REPORT;
            event.source = EVENT_SOURCE_NODE;
            event.node_id = message.node_id;
            event.tick = message.data.tick_report.tick;
            event.task_id = message.data.tick_report.task_id;
        } else {
            continue;
        }
        event.host_time_ns = host_time_ns();
        record_node_event_tick(&event);
        node_reports_drained[i] = false;
        if (!publish_report_event(queue, &event))
            success = false;
    }
    return success;
}

/**
 * @brief Envía START o STOP a ambos nodos y espera sus respuestas.
 *
 * Envía a los dos enlaces antes de esperar cada ACK/NACK. El resultado de un
 * nodo no cancela ni revierte la operación del otro; se devuelve true solo
 * cuando ambos aceptaron el comando.
 *
 * @param type     Tipo de comando, MSG_START o MSG_STOP.
 * @param request_id Identificador común de correlación para este comando.
 * @param node_ids Identificadores de los nodos.
 * @param devices  Rutas o nombres de los dispositivos seriales.
 * @param handlers Enlaces de comunicación activos.
 * @param sent     Buffers JSON de envío, uno por dispositivo.
 *
 * @return true si ambos nodos aceptaron el comando.
 */
static bool send_control_to_nodes(
    EventQueue *queue,
    MessageType type,
    uint32_t request_id,
    const uint32_t node_ids[EXPECTED_DEVICES],
    const char *devices[EXPECTED_DEVICES],
    comms_handler_t handlers[EXPECTED_DEVICES],
    char sent[EXPECTED_DEVICES][COMMS_MESSAGE_CAPACITY],
    bool running[EXPECTED_DEVICES],
    unsigned int timeout_seconds
)
{
    const char *name = type == MSG_START ? "START" : "STOP";
    bool sent_to_node[EXPECTED_DEVICES] = {false, false};
    bool all_accepted = true;

    for (size_t i = 0u; i < EXPECTED_DEVICES; ++i) {
        Message request = {
            .type = type,
            .node_id = node_ids[i],
            .request_id = request_id
        };
        sent_to_node[i] = send_message(
            &handlers[i], sent[i], &request, node_ids[i], name
        );
        if (!sent_to_node[i])
            all_accepted = false;
    }

    for (size_t i = 0u; i < EXPECTED_DEVICES; ++i) {
        if (sent_to_node[i]) {
            bool accepted = wait_for_control_reply(
                queue, &handlers[i], node_ids[i], request_id, type, devices[i],
                timeout_seconds
            );
            if (!accepted) {
                all_accepted = false;
                continue;
            }
            running[i] = type == MSG_START;
            if (type == MSG_START) {
                node_started_once[i] = true;
                Event event = {
                    .type = EVENT_NODE_STARTED,
                    .source = EVENT_SOURCE_HOST,
                    .node_id = node_ids[i],
                    .tick = latest_node_ticks[i],
                    .host_time_ns = host_time_ns()
                };
                if (!publish_report_event(queue, &event))
                    all_accepted = false;
            } else {
                node_reports_drained[i] = false;
                node_stop_pending[i] = node_started_once[i];
            }
        }
    }
    if (type == MSG_STOP) {
        bool required[EXPECTED_DEVICES] = {
            node_started_once[0], node_started_once[1]
        };
        if (!drain_node_reports(queue, node_ids, handlers, required))
            all_accepted = false;
    }
    return all_accepted;
}

/**
 * @brief Envía START o STOP a un único nodo y espera su respuesta.
 *
 * @param type       Tipo de comando, MSG_START o MSG_STOP.
 * @param request_id Identificador de correlación del comando.
 * @param node_index Índice del enlace y nodo a controlar.
 * @param node_ids   Identificadores de los nodos.
 * @param devices    Rutas o nombres de los dispositivos seriales.
 * @param handlers   Enlaces de comunicación activos.
 * @param sent       Buffers JSON de envío, uno por dispositivo.
 *
 * @return true si el nodo seleccionado respondió ACK.
 */
static bool send_control_to_node(
    EventQueue *queue,
    MessageType type,
    uint32_t request_id,
    size_t node_index,
    const uint32_t node_ids[EXPECTED_DEVICES],
    const char *devices[EXPECTED_DEVICES],
    comms_handler_t handlers[EXPECTED_DEVICES],
    char sent[EXPECTED_DEVICES][COMMS_MESSAGE_CAPACITY],
    bool running[EXPECTED_DEVICES],
    unsigned int timeout_seconds
)
{
    const char *name = type == MSG_START ? "START" : "STOP";
    Message request = {
        .type = type,
        .node_id = node_ids[node_index],
        .request_id = request_id
    };

    if (!send_message(&handlers[node_index], sent[node_index], &request,
                      node_ids[node_index], name)) {
        return false;
    }

    bool accepted = wait_for_control_reply(
        queue, &handlers[node_index], node_ids[node_index], request_id, type,
        devices[node_index], timeout_seconds
    );
    if (!accepted)
        return false;

    running[node_index] = type == MSG_START;
    if (type == MSG_START) {
        node_started_once[node_index] = true;
        Event event = {
            .type = EVENT_NODE_STARTED,
            .source = EVENT_SOURCE_HOST,
            .node_id = node_ids[node_index],
            .tick = latest_node_ticks[node_index],
            .host_time_ns = host_time_ns()
        };
        return publish_report_event(queue, &event);
    } else {
        node_reports_drained[node_index] = false;
        node_stop_pending[node_index] = node_started_once[node_index];
        bool required[EXPECTED_DEVICES] = {false, false};
        required[node_index] = node_started_once[node_index];
        return drain_node_reports(queue, node_ids, handlers, required);
    }
}

/**
 * @brief Obtiene un request_id de control y prepara el siguiente.
 *
 * Los identificadores de control usan un rango separado de CONFIG,
 * calibración y despliegue.
 *
 * @param next_request_id Contador que se consume y luego se actualiza.
 *
 * @return Identificador asignado a la solicitud actual.
 */
static uint32_t take_control_request_id(uint32_t *next_request_id)
{
    uint32_t request_id = *next_request_id;
    *next_request_id = request_id == UINT32_MAX
        ? FIRST_CONTROL_REQUEST_ID : request_id + 1u;
    return request_id;
}

/**
 * @brief Lee comandos interactivos y controla los schedulers remotos.
 *
 * Acepta `start`/`stop` para ambos nodos, `start 1`/`stop 1` para ESP32,
 * `start 2`/`stop 2` para Pico, y `q` para solicitar cierre. Las respuestas
 * se procesan independientemente por nodo mientras llegan los reportes.
 *
 * @param node_ids Identificadores de los nodos.
 * @param devices  Rutas o nombres de los dispositivos seriales.
 * @param handlers Enlaces de comunicación que permanecen abiertos durante
 *                 la consola.
 * @param sent     Buffers JSON de envío, uno por dispositivo.
 */
static void run_control_console(
    EventQueue *queue,
    const uint32_t node_ids[EXPECTED_DEVICES],
    const char *devices[EXPECTED_DEVICES],
    comms_handler_t handlers[EXPECTED_DEVICES],
    char sent[EXPECTED_DEVICES][COMMS_MESSAGE_CAPACITY],
    bool running[EXPECTED_DEVICES]
)
{
    char line[64];
    size_t used = 0u;
    uint32_t next_request_id = FIRST_CONTROL_REQUEST_ID;

    puts("Comandos: start [1|2], stop [1|2], q.");
    fputs("> ", stdout);
    fflush(stdout);
    while (!event_queue_stop_requested(queue)) {
        (void)pump_node_reports(queue, node_ids, handlers);
        struct pollfd input = {.fd = STDIN_FILENO, .events = POLLIN};
        int ready = poll(&input, 1u, 20);
        if (ready < 0) {
            if (errno == EINTR)
                continue;
            perror("poll stdin");
            event_queue_request_stop(queue);
            break;
        }
        if (ready == 0)
            continue;
        if (input.revents & (POLLERR | POLLNVAL)) {
            fprintf(stderr, "Error leyendo comandos de control.\n");
            event_queue_request_stop(queue);
            break;
        }
        if (!(input.revents & (POLLIN | POLLHUP)))
            continue;
        char character;
        ssize_t bytes = read(STDIN_FILENO, &character, 1u);
        if (bytes == 0) {
            event_queue_request_stop(queue);
            break;
        }
        if (bytes < 0) {
            if (errno == EINTR)
                continue;
            perror("read stdin");
            event_queue_request_stop(queue);
            break;
        }
        if (character != '\n' && character != '\r') {
            if (used + 1u < sizeof(line))
                line[used++] = character;
            continue;
        }
        line[used] = '\0';
        used = 0u;
        if (strcmp(line, "start") == 0) {
            uint32_t request_id = take_control_request_id(&next_request_id);
            (void)send_control_to_nodes(queue, MSG_START, request_id,
                node_ids, devices, handlers, sent, running,
                HANDSHAKE_TIMEOUT_SECONDS);
        } else if (strcmp(line, "stop") == 0) {
            uint32_t request_id = take_control_request_id(&next_request_id);
            (void)send_control_to_nodes(queue, MSG_STOP, request_id,
                node_ids, devices, handlers, sent, running,
                HANDSHAKE_TIMEOUT_SECONDS);
        } else if (strcmp(line, "start 1") == 0 ||
                   strcmp(line, "start 2") == 0 ||
                   strcmp(line, "stop 1") == 0 ||
                   strcmp(line, "stop 2") == 0) {
            const bool starting =
                strcmp(line, "start 1") == 0 ||
                strcmp(line, "start 2") == 0;
            const size_t node_index = line[strlen(line) - 1u] == '1' ? 0u : 1u;
            uint32_t request_id = take_control_request_id(&next_request_id);
            (void)send_control_to_node(
                queue, starting ? MSG_START : MSG_STOP, request_id,
                node_index, node_ids, devices, handlers, sent, running,
                HANDSHAKE_TIMEOUT_SECONDS
            );
        } else if (strcmp(line, "q") == 0) {
            event_queue_request_stop(queue);
            break;
        } else if (line[0] != '\0') {
            puts("Comando inválido. Usa start [1|2], stop [1|2] o q.");
        }
        fputs("> ", stdout);
        fflush(stdout);
    }
}

/**
 * @brief Carga, planifica y despliega las tareas admitidas por el Host.
 *
 * Inicializa el Planner con las capacidades calibradas, asigna cada tarea,
 * envía DEPLOY_TASK al nodo correspondiente y espera su ACK/NACK. Si un nodo
 * rechaza una tarea, revierte su reserva local para mantener coherente el
 * planificador.
 *
 * @param config     Configuración del algoritmo, heurística y archivo CSV.
 * @param node_ids   Identificadores de los nodos disponibles.
 * @param devices    Rutas de los puertos seriales.
 * @param handlers   Enlaces de comunicación activos.
 * @param sent       Buffers JSON de envío, uno por dispositivo.
 * @param ut_per_tick Capacidades medidas de los nodos.
 *
 * @return true si la carga y el proceso de despliegue terminaron correctamente.
 */
static bool plan_and_deploy_tasks(
    EventQueue *queue,
    const HostConfig *config,
    const uint32_t node_ids[EXPECTED_DEVICES],
    const char *devices[EXPECTED_DEVICES],
    comms_handler_t handlers[EXPECTED_DEVICES],
    char sent[EXPECTED_DEVICES][COMMS_MESSAGE_CAPACITY],
    const uint32_t ut_per_tick[EXPECTED_DEVICES]
)
{
    TaskList tasks = {0};
    PlannerDecision *decisions = NULL;
    Planner planner;
    bool success = false;
    char error[256];

    /* Carga CSV y crea el plan a partir de las capacidades medidas en esta sesión. */
    if (!task_parser_load(config->tasks_file, &tasks, error, sizeof(error))) {
        fprintf(stderr, "No se pudo cargar %s: %s\n",
            config->tasks_file, error);
        goto cleanup;
    }
    if (tasks.count > FIRST_CONTROL_REQUEST_ID - FIRST_DEPLOY_REQUEST_ID) {
        fprintf(stderr, "El archivo contiene demasiadas tareas para request_id.\n");
        goto cleanup;
    }
    if (tasks.count > 0u) {
        decisions = calloc(tasks.count, sizeof(*decisions));
        if (decisions == NULL) {
            fprintf(stderr, "No hay memoria para planificar las tareas.\n");
            goto cleanup;
        }
    }

    if (!planner_init(&planner, config->algorithm, config->heuristic)) {
        fprintf(stderr, "No se pudo inicializar el planificador.\n");
        goto cleanup;
    }
    for (size_t i = 0u; i < EXPECTED_DEVICES; ++i) {
        if (!planner_add_node(&planner, node_ids[i], ut_per_tick[i])) {
            fprintf(stderr, "No se pudo registrar el nodo %u en el planificador.\n",
                node_ids[i]);
            goto cleanup;
        }
    }
    if (!planner_plan_batch(&planner, tasks.items, tasks.count, decisions)) {
        fprintf(stderr, "No se pudo planificar el lote de tareas.\n");
        goto cleanup;
    }

    printf("Planificación: %zu tarea(s) cargada(s) desde %s.\n",
        tasks.count, config->tasks_file);
    /*
     * task.id identifica la tarea; node_id define el destino y request_id
     * correlaciona cada despliegue. Se envía y confirma de una en una para
     * reutilizar con seguridad el buffer de envío de cada controlador.
     */
    uint32_t request_id = FIRST_DEPLOY_REQUEST_ID;
    size_t deployed_count = 0u;

    for (size_t i = 0u; i < tasks.count; ++i) {
        PlannerDecision *decision = &decisions[i];
        if (!decision->accepted) {
            Event event = {
                .type = EVENT_TASK_REJECTED,
                .source = EVENT_SOURCE_HOST,
                .node_id = decision->node_id,
                .task_id = tasks.items[i].id,
                .detail = decision->reason,
                .task = tasks.items[i],
                .host_time_ns = host_time_ns()
            };
            if (!publish_report_event(queue, &event))
                goto cleanup;
            printf("Tarea %" PRIu32 " no admitida por el planificador: %s.\n",
                tasks.items[i].id, report_reject_reason(decision->reason));
            continue;
        }

        size_t node_index = 0u;
        while (node_index < EXPECTED_DEVICES &&
               node_ids[node_index] != decision->node_id)
            ++node_index;
        if (node_index == EXPECTED_DEVICES) {
            fprintf(stderr, "El planificador asignó la tarea a un nodo desconocido.\n");
            goto cleanup;
        }

        Message request = {
            .type = MSG_DEPLOY_TASK,
            .node_id = decision->node_id,
            .request_id = request_id,
            .data.deploy_task = {
                .task = tasks.items[i],
                .cost_ticks = decision->cost_ticks
            }
        };
        if (!send_message(&handlers[node_index], sent[node_index], &request,
                          decision->node_id, "DEPLOY_TASK")) {
            goto cleanup;
        }

        bool accepted = false;
        RejectReason reason = REJECT_NONE;
        if (!wait_for_task_deploy(&handlers[node_index], decision->node_id,
                                  request_id, devices[node_index],
                                  &accepted, &reason)) {
            goto cleanup;
        }

        if (accepted) {
            ++deployed_count;
            Event event = {
                .type = EVENT_TASK_ACCEPTED,
                .source = EVENT_SOURCE_HOST,
                .node_id = decision->node_id,
                .task_id = decision->task_id,
                .cost_ticks = decision->cost_ticks,
                .task = tasks.items[i],
                .host_time_ns = host_time_ns()
            };
            if (!publish_report_event(queue, &event))
                goto cleanup;
            printf("Tarea %" PRIu32 " almacenada en nodo %" PRIu32
                   " (costo %" PRIu32 " tick(s)).\n",
                decision->task_id, decision->node_id, decision->cost_ticks);
        } else {
            /* Deshacer la reserva del Planner para reflejar el NACK del MCU. */
            if (!planner_remove_task(&planner, decision->node_id,
                                     decision->task_id)) {
                fprintf(stderr,
                    "No se pudo revertir la asignación local de tarea %" PRIu32 ".\n",
                    decision->task_id);
                goto cleanup;
            }
            Event event = {
                .type = EVENT_TASK_REJECTED,
                .source = EVENT_SOURCE_NODE,
                .node_id = decision->node_id,
                .task_id = decision->task_id,
                .detail = reason,
                .task = tasks.items[i],
                .host_time_ns = host_time_ns()
            };
            if (!publish_report_event(queue, &event))
                goto cleanup;
            printf("Nodo %" PRIu32 " rechazó tarea %" PRIu32 ": %s.\n",
                decision->node_id, decision->task_id,
                report_reject_reason(reason));
        }
        if (request_id != UINT32_MAX)
            ++request_id;
    }

    printf("Despliegue terminado: %zu de %zu tarea(s) almacenada(s); "
           "las tareas no se han iniciado.\n",
        deployed_count, tasks.count);
    success = true;

cleanup:
    free(decisions);
    task_list_free(&tasks);
    return success;
}

/**
 * @brief Solicita el cierre de los enlaces y espera que terminen sus hilos.
 *
 * Primero detiene todos los handlers y después revisa bajo mutex el estado de
 * cada hilo hasta que finalice.
 *
 * @param handlers Arreglo de handlers de comunicación.
 * @param count    Cantidad de handlers que deben cerrarse.
 */
static void stop_handlers(
    comms_handler_t *handlers,
    const bool *handler_started,
    size_t count
)
{
    for (size_t i = 0u; i < count; ++i) {
        if (handler_started[i])
            comms_stop_handler(&handlers[i]);
    }

    for (size_t i = 0u; i < count; ++i) {
        if (!handler_started[i])
            continue;
        for (;;) {
            pthread_mutex_lock(&handlers[i].mutex);
            bool finished = handlers[i].finished;
            pthread_mutex_unlock(&handlers[i].mutex);
            if (finished)
                break;
            struct timespec pause = {.tv_nsec = 10000000L};
            nanosleep(&pause, NULL);
        }
    }
}

/**
 * @brief Espera a que los nodos requeridos confirmen el vaciado de sus reportes.
 *
 * Procesa los eventos pendientes mientras espera un PONG por nodo y falla si
 * no se confirma el drenado dentro del límite de espera.
 *
 * @param queue Cola donde se publicarán los reportes recibidos.
 * @param node_ids Identificadores de los nodos.
 * @param handlers Enlaces seriales que se inspeccionarán.
 * @param required Indica qué nodos deben confirmar el drenado.
 *
 * @return true si todos los nodos requeridos confirmaron el vaciado.
 */
static bool drain_node_reports(
    EventQueue *queue,
    const uint32_t node_ids[EXPECTED_DEVICES],
    comms_handler_t handlers[EXPECTED_DEVICES],
    const bool required[EXPECTED_DEVICES]
)
{
    uint64_t start = monotonic_time_ms();
    for (;;) {
        bool drained = true;
        for (size_t i = 0u; i < EXPECTED_DEVICES; ++i) {
            if (required[i] && !node_reports_drained[i])
                drained = false;
        }
        if (drained)
            return true;
        (void)pump_node_reports(queue, node_ids, handlers);
        if (monotonic_time_ms() - start >= 2000u)
            break;
        struct timespec pause = {.tv_nsec = 10000000L};
        nanosleep(&pause, NULL);
    }

    for (size_t i = 0u; i < EXPECTED_DEVICES; ++i) {
        if (required[i] && !node_reports_drained[i]) {
            fprintf(stderr, "No se vació la cola de eventos del nodo %u antes del cierre.\n",
                node_ids[i]);
            return false;
        }
    }
    return true;
}

/**
 * @brief Configura los nodos, calibra capacidades y despliega las tareas.
 *
 * Al completar el despliegue inicia la consola interactiva de control. La
 * función se ejecuta en el worker propietario de los enlaces seriales.
 *
 * @param queue Cola compartida para publicar los eventos de la sesión.
 * @param context Contexto de sesión serial recibido por ReportWorker.
 *
 * @return Código cero si la configuración y el despliegue terminan bien.
 */
static int serial_report_worker_run(EventQueue *queue, void *context)
{
    SerialReportSession *session = context;
    memset(latest_node_ticks, 0, sizeof(latest_node_ticks));
    memset(node_started_once, 0, sizeof(node_started_once));
    memset(node_reports_drained, 0, sizeof(node_reports_drained));
    memset(node_stop_pending, 0, sizeof(node_stop_pending));
    bool acked[EXPECTED_DEVICES] = {false, false};
    bool calibrated[EXPECTED_DEVICES] = {false, false};
    uint32_t ut_per_tick[EXPECTED_DEVICES] = {0u, 0u};

    for (size_t i = 0u; i < EXPECTED_DEVICES; ++i) {
        if (comms_init_handler((int)i, &session->handlers[i],
                               session->devices[i], 0u,
                               session->config.tick_ms,
                               session->config.missed_reports_limit)
            != COMMS_OK) {
            fprintf(stderr, "No se pudo inicializar el enlace %s.\n",
                session->devices[i]);
            return 1;
        }
        session->initialized[i] = true;
        session->initialized_count = i + 1u;
        session->handlers[i].sent.content = session->sent[i];
        session->handlers[i].recv.content = session->received[i];

        Message request = {
            .type = MSG_CONFIG,
            .node_id = session->node_ids[i],
            .request_id = 1u,
            .data.config = {
                .algorithm = session->config.algorithm == SCHED_EDF
                    ? MESSAGE_SCHED_EDF : MESSAGE_SCHED_RMS,
                .tick_ms = session->config.tick_ms,
                .report_every_ticks = session->config.report_every_ticks
            }
        };
        if (!send_message(&session->handlers[i], session->sent[i], &request,
                          session->node_ids[i], "CONFIG")) {
            return 1;
        }
        if (comms_setup_handler(&session->handlers[i]) != COMMS_OK) {
            fprintf(stderr, "No se pudo iniciar el enlace %s.\n",
                session->devices[i]);
            return 1;
        }
        session->handler_started[i] = true;
    }

    for (size_t i = 0u; i < EXPECTED_DEVICES; ++i)
        acked[i] = wait_for_ack(&session->handlers[i], session->node_ids[i],
                                1u, session->devices[i]);

    if (!acked[0] || !acked[1]) {
        fprintf(stderr, "Intercambio CONFIG incompleto; revise conexiones y firmware.\n");
        return 1;
    }

    for (size_t i = 0u; i < EXPECTED_DEVICES; ++i) {
        Message request = {
            .type = MSG_CALIBRATE_REQUEST,
            .node_id = session->node_ids[i],
            .request_id = CALIBRATION_REQUEST_ID,
            .data.sample_ticks = CALIBRATION_DEFAULT_SAMPLES
        };
        if (!send_message(&session->handlers[i], session->sent[i], &request,
                          session->node_ids[i],
                          "CALIBRATE_REQUEST")) {
            return 1;
        }
    }

    for (size_t i = 0u; i < EXPECTED_DEVICES; ++i) {
        calibrated[i] = wait_for_calibration(
            &session->handlers[i], session->node_ids[i],
            CALIBRATION_REQUEST_ID, session->devices[i], &ut_per_tick[i]
        );
    }

    if (!calibrated[0] || !calibrated[1]) {
        fprintf(stderr, "Calibración incompleta; revise los firmwares y la conexión.\n");
        return 1;
    }

    for (size_t i = 0u; i < EXPECTED_DEVICES; ++i) {
        Event event = {
            .type = EVENT_NODE_CALIBRATED,
            .source = EVENT_SOURCE_HOST,
            .node_id = session->node_ids[i],
            .ut_per_tick = ut_per_tick[i],
            .host_time_ns = host_time_ns()
        };
        if (!publish_report_event(queue, &event))
            return 1;
    }

    bool deployed = plan_and_deploy_tasks(
        queue, &session->config, session->node_ids, session->devices,
        session->handlers, session->sent, ut_per_tick
    );
    if (!deployed) {
        return 1;
    }

    puts("CONFIG, calibración y despliegue de tareas completados.");
    run_control_console(queue, session->node_ids, session->devices,
                        session->handlers, session->sent, session->running);
    return 0;
}

/**
 * @brief Detiene los nodos activos, drena sus eventos y cierra los enlaces.
 *
 * @param queue Cola compartida donde se publican los últimos eventos.
 * @param context Contexto de sesión serial recibido por ReportWorker.
 *
 * @return Código cero si el cierre y el drenado terminaron correctamente.
 */
static int serial_report_worker_stop(EventQueue *queue, void *context)
{
    SerialReportSession *session = context;
    bool success = true;
    uint32_t request_id = FIRST_CONTROL_REQUEST_ID;
    for (size_t i = 0u; i < EXPECTED_DEVICES; ++i) {
        if (!session->initialized[i] || !session->handler_started[i] ||
            !session->running[i])
            continue;
        if (!send_control_to_node(
                queue, MSG_STOP, request_id++, i, session->node_ids,
                session->devices, session->handlers, session->sent,
                session->running, 1u)) {
            Event event = {
                .type = EVENT_NODE_FAILED,
                .source = EVENT_SOURCE_HOST,
                .node_id = session->node_ids[i],
                .host_time_ns = host_time_ns()
            };
            (void)publish_report_event(queue, &event);
            success = false;
        }
    }
    bool required[EXPECTED_DEVICES] = {
        node_started_once[0], node_started_once[1]
    };
    if (!drain_node_reports(queue, session->node_ids, session->handlers, required))
        success = false;
    stop_handlers(session->handlers, session->handler_started,
                  session->initialized_count);
    return success ? 0 : 1;
}

/**
 * @brief Arranca una sesión serial con reportes CSV/HTML al finalizar.
 *
 * Los comandos `start` y `stop` se conservan; `q` detiene los nodos y cierra
 * la sesión para generar el reporte.
 *
 * @param argc Cantidad de argumentos de la línea de comandos.
 * @param argv Argumentos: puertos seriales ESP32 y Pico, o modo de demo.
 *
 * @return Código de salida de la sesión.
 */
int main(int argc, char *argv[])
{
    if (argc >= 2 && strcmp(argv[1], "--report-demo") == 0) {
        if (argc > 3) {
            fprintf(stderr, "Usage: %s --report-demo [output_directory]\n", argv[0]);
            return 1;
        }
        char directory[128];
        if (argc == 2 && mkdir("output", 0775) != 0 && errno != EEXIST) {
            perror("output");
            return 1;
        }
        snprintf(directory, sizeof(directory), "output/report_%ld_%ld",
            (long)time(NULL), (long)getpid());
        return report_demo_run(argc == 3 ? argv[2] : directory);
    }

    if (argc != 1 + EXPECTED_DEVICES) {
        fprintf(stderr, "Usage: %s <ESP32 serial port> <Pico serial port>\n", argv[0]);
        return 1;
    }

    SerialReportSession session = {0};
    char error[256];
    if (!config_load("config/system.conf", &session.config, error, sizeof(error))) {
        fprintf(stderr, "No se pudo cargar config/system.conf: %s\n", error);
        return 1;
    }
    session.devices[0] = argv[1];
    session.devices[1] = argv[2];
    session.node_ids[0] = 1u;
    session.node_ids[1] = 2u;

    if (mkdir("output", 0775) != 0 && errno != EEXIST) {
        perror("output");
        return 1;
    }
    char directory[128];
    int written = snprintf(directory, sizeof(directory), "output/report_%ld_%ld",
        (long)time(NULL), (long)getpid());
    if (written < 0 || (size_t)written >= sizeof(directory)) {
        fprintf(stderr, "No se pudo construir la ruta del reporte.\n");
        return 1;
    }

    ReportWorker worker = {
        .run = serial_report_worker_run,
        .stop = serial_report_worker_stop,
        .context = &session,
        .worker_reads_stdin = true
    };
    printf("Generando reporte de sesión en %s; use q para finalizar.\n", directory);
    int result = report_session_run(directory, &worker, 1u);
    if (result == 0)
        printf("Sesión cerrada. Reporte HTML y CSV en %s.\n", directory);
    else
        fprintf(stderr, "La sesión terminó con errores; revise el reporte en %s.\n",
            directory);
    return result;
}