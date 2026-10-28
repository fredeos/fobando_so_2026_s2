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
#include "host/workers/node_worker.h"
#include "host/event_queue.h"

#include "common/message_parser.h"
#include "host/config.h"
#include "host/manual_tasks.h"
#include "host/failover.h"
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

/* Estado que el padre necesita para comunicarse con ambos workers. */
typedef struct {
    HostConfig config;
    const char *devices[EXPECTED_DEVICES];
    uint32_t node_ids[EXPECTED_DEVICES];
    int command_write_fd[EXPECTED_DEVICES];
    int reply_read_fd[EXPECTED_DEVICES];
    int link_status_read_fd[EXPECTED_DEVICES];
    int pipe_fds[EXPECTED_DEVICES * 6u];
    NodeWorkerContext workers[EXPECTED_DEVICES];
    bool node_online[EXPECTED_DEVICES];
    bool node_should_run[EXPECTED_DEVICES];
    bool failover_active[EXPECTED_DEVICES];
    FailoverResult failovers[EXPECTED_DEVICES];
    uint32_t next_control_request_id;
    Planner planner;
    uint32_t next_deploy_request_id;
} HostCoordinatorContext;

/**
 * @brief Publica un evento para que el proceso reporter lo consuma.
 * Reintenta si la cola está llena temporalmente y marca la sesión incompleta
 * cuando ya no es posible publicar.
 */
bool publish_report_event(EventQueue *queue, const Event *event)
{
    EventQueueResult result;
    do {
        result = event_queue_push(queue, event, 100);
    } while (result == EVENT_QUEUE_TIMEOUT && !event_queue_stop_requested(queue));
    if (result == EVENT_QUEUE_OK)
        return true;
    event_queue_mark_incomplete(queue);
    fprintf(stderr, "No se pudo publicar un evento en la cola compartida.\n");
    return false;
}

/**
 * @brief Actualiza el último tick conocido de un nodo a partir de un evento.
 *
 * Para eventos de ejecución e inactividad considera también la duración del
 * intervalo reportado.
 *
 * @param event Evento recibido desde el nodo.
 */
/* Guarda el tick final observado para etiquetar correctamente el cierre. */
static void record_node_event_tick(NodeSerialContext *serial, Event *event)
{
    size_t index = event->node_id == 1u ? 0u : event->node_id == 2u ? 1u :
        EXPECTED_DEVICES;
    if (index >= EXPECTED_DEVICES)
        return;
    if (UINT64_MAX - event->tick >= serial->tick_offset[index])
        event->tick += serial->tick_offset[index];
    uint64_t end = event->tick;
    if ((event->type == EVENT_EXECUTION_SLICE ||
         event->type == EVENT_IDLE_SLICE) &&
        UINT64_MAX - end >= event->duration_ticks)
        end += event->duration_ticks;
    if (end > serial->latest_ticks[index])
        serial->latest_ticks[index] = end;
}

/**
 * @brief Obtiene el tiempo monotónico actual en nanosegundos.
 *
 * @return Tiempo en nanosegundos, o cero si falla la lectura del reloj.
 */
uint64_t host_time_ns(void)
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
uint64_t monotonic_time_ms(void)
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
/* Serializa el protocolo de aplicación; comms se encarga del framing serial. */
bool send_message(
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
 * @brief Extrae el siguiente mensaje recibido por el handler serial.
 *
 * Diferencia una cola vacía de un error de recepción, como el desbordamiento
 * de la cola, para que el llamador no confunda pérdidas con ausencia temporal
 * de mensajes.
 *
 * @param handler Enlace serial que contiene el mensaje.
 * @param buffer Buffer para copiar el payload.
 * @param capacity Capacidad del buffer.
 * @param message_size Recibe la cantidad de bytes copiados.
 * @param ready Indica si había un mensaje disponible.
 *
 * @return true si la consulta terminó correctamente; false ante un error.
 */
/* Una cola vacía no es error; un overflow sí puede implicar pérdida de datos. */
static bool take_received_message(
    comms_handler_t *handler,
    char *buffer,
    size_t capacity,
    size_t *message_size,
    bool *ready
)
{
    comms_status_t status = comms_receive_next(
        handler, buffer, capacity, message_size
    );
    if (status == COMMS_BUSY) {
        *ready = false;
        return true;
    }
    if (status != COMMS_OK) {
        fprintf(stderr, "Error extrayendo mensaje serial de %s (estado %d).\n",
            handler->dev, (int)status);
        return false;
    }
    *ready = true;
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
bool wait_for_ack(
    comms_handler_t *handler,
    uint32_t node_id,
    uint32_t request_id,
    const char *device,
    uint64_t *round_trip_ns)
{
    const uint64_t start_ns = worker_monotonic_time_ns();

    for (;;) {
        char response[COMMS_MESSAGE_CAPACITY];
        size_t response_size = 0u;
        bool ready = false;

        if (!take_received_message(
                handler, response, sizeof(response),
                &response_size, &ready)) {
            return false;
        }

        if (ready) {
            if (response_size > 0u &&
                response[response_size - 1u] == '\0') {
                --response_size;
            }

            Message reply;
            MessageParseResult parsed =
                message_parse(response, response_size, &reply);

            if (parsed == MESSAGE_PARSE_OK &&
                reply.type == MSG_ACK &&
                reply.node_id == node_id &&
                reply.request_id == request_id &&
                reply.data.reply.type == MSG_CONFIG) {

                const uint64_t end_ns = worker_monotonic_time_ns();
                const uint64_t elapsed_ns =
                    end_ns >= start_ns ? end_ns - start_ns : 0u;

                if (round_trip_ns != NULL)
                    *round_trip_ns = elapsed_ns;

                comms_acquire_send(handler);

                printf(
                    "CONFIG confirmado por nodo %" PRIu32
                    " (%s): RTT = %" PRIu64 " ns (%.3f ms).\n",
                    node_id,
                    device,
                    elapsed_ns,
                    (double)elapsed_ns / 1000000.0
                );

                return true;
            }

            /*
             * Preserve your existing handling for unexpected messages,
             * NACKs, and malformed responses here if you have it.
             */
        }

        const uint64_t now_ns = worker_monotonic_time_ns();
        const uint64_t timeout_ns =
            (uint64_t)HANDSHAKE_TIMEOUT_SECONDS * 1000000000ull;

        if (now_ns >= start_ns && now_ns - start_ns >= timeout_ns) {
            fprintf(
                stderr,
                "Timeout esperando ACK de CONFIG del nodo %" PRIu32
                " (%s).\n",
                node_id, device
            );
            return false;
        }

        const struct timespec pause = {
            .tv_sec = 0,
            .tv_nsec = 1000000L
        };
        nanosleep(&pause, NULL);
    }
}

/* Espera ACK/NACK de CONFIG y conserva los reportes que lleguen entre ambos. */
bool wait_for_config_reply(
    EventQueue *queue,
    NodeSerialContext *serial,
    size_t index,
    comms_handler_t *handler,
    uint32_t node_id,
    uint32_t request_id,
    const char *device,
    uint64_t send_time_ns,
    uint64_t *round_trip_ns,
    bool *accepted,
    RejectReason *reason
)
{
    for (;;) {
        char response[COMMS_MESSAGE_CAPACITY];
        size_t response_size = 0u;
        bool ready = false;

        if (!take_received_message(
                handler, response, sizeof(response),
                &response_size, &ready)) {
            return false;
        }

        if (ready) {
            if (response_size > 0u &&
                response[response_size - 1u] == '\0') {
                --response_size;
            }

            Message reply;
            MessageParseResult parsed =
                message_parse(response, response_size, &reply);

            if (parsed == MESSAGE_PARSE_OK &&
                reply.node_id == node_id &&
                reply.request_id == request_id &&
                (reply.type == MSG_ACK || reply.type == MSG_NACK) &&
                reply.data.reply.type == MSG_CONFIG) {

                const uint64_t end_ns = worker_monotonic_time_ns();

                if (round_trip_ns != NULL) {
                    *round_trip_ns = end_ns >= send_time_ns
                        ? end_ns - send_time_ns
                        : 0u;
                }

                *accepted = reply.type == MSG_ACK;
                *reason = *accepted
                    ? REJECT_NONE
                    : reply.data.reply.reason;

                comms_acquire_send(handler);

                if (*accepted) {
                    printf(
                        "CONFIG confirmado por nodo %" PRIu32
                        " (%s): RTT = %" PRIu64
                        " ns (%.3f ms).\n",
                        node_id,
                        device,
                        round_trip_ns != NULL ? *round_trip_ns : 0u,
                        (double)(round_trip_ns != NULL
                            ? *round_trip_ns : 0u) / 1000000.0
                    );
                }

                return true;
            }

            /*
             * A report can arrive before the CONFIG reply.
             * Do not discard it; publish it and count it for the watchdog.
             */
            if (parsed == MESSAGE_PARSE_OK &&
                reply.node_id == node_id &&
                (reply.type == MSG_EVENT ||
                 reply.type == MSG_TICK_REPORT)) {

                Event event = reply.type == MSG_EVENT
                    ? reply.data.event
                    : (Event) {
                        .type = EVENT_TICK_REPORT,
                        .source = EVENT_SOURCE_NODE,
                        .node_id = reply.node_id,
                        .tick = reply.data.tick_report.tick,
                        .task_id = reply.data.tick_report.task_id
                    };

                uint64_t report_time_ns = worker_monotonic_time_ns();

                record_node_event_tick(serial, &event);
                record_report_watchdog(serial, index, report_time_ns);

                event.host_time_ns = host_time_ns();

                if (!publish_report_event(queue, &event))
                    return false;
            }
        }

        const uint64_t now_ns = worker_monotonic_time_ns();
        const uint64_t timeout_ns =
            (uint64_t)HANDSHAKE_TIMEOUT_SECONDS * 1000000000ull;

        if (now_ns >= send_time_ns && now_ns - send_time_ns >= timeout_ns) {
            fprintf(
                stderr,
                "Timeout esperando respuesta CONFIG del nodo %" PRIu32
                " (%s).\n",
                node_id, device
            );
            return false;
        }

        const struct timespec pause = {
            .tv_sec = 0,
            .tv_nsec = 1000000L
        };
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
bool wait_for_calibration(
    comms_handler_t *handler,
    uint32_t node_id,
    uint32_t request_id,
    const char *device,
    uint32_t *ut_per_tick
)
{
    time_t start = time(NULL);
    for (;;) {
        /* Ignorar mensajes de otra solicitud: se correlaciona por nodo e ID. */
        char response[COMMS_MESSAGE_CAPACITY];
        size_t response_size = 0u;
        bool ready;
        if (!take_received_message(handler, response, sizeof(response),
                                   &response_size, &ready)) {
            return false;
        }

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
bool wait_for_task_deploy(
    EventQueue *queue,
    NodeSerialContext *serial,
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
        /* Mientras llega ACK/NACK, también pueden llegar eventos del scheduler. */
        char response[COMMS_MESSAGE_CAPACITY];
        size_t response_size = 0u;
        bool ready;
        if (!take_received_message(handler, response, sizeof(response),
                                   &response_size, &ready))
            return false;

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
                record_node_event_tick(serial, &event);
                size_t index = node_id == 1u ? 0u : node_id == 2u ? 1u :
                    EXPECTED_DEVICES;
                if (index < EXPECTED_DEVICES)
                    serial->reports_drained[index] = false;
                if (!publish_report_event(queue, &event))
                    return false;
                continue;
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
bool wait_for_control_reply(
    EventQueue *queue,
    NodeSerialContext *serial,
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
        /* El worker serial pone las respuestas y reportes en la FIFO del handler. */
        char response[COMMS_MESSAGE_CAPACITY];
        size_t response_size = 0u;
        bool ready;
        if (!take_received_message(handler, response, sizeof(response),
                                   &response_size, &ready)) {
            event_queue_mark_incomplete(queue);
            return false;
        }

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
                record_node_event_tick(serial, &event);
                size_t index = node_id == 1u ? 0u : node_id == 2u ? 1u :
                    EXPECTED_DEVICES;
                if (index < EXPECTED_DEVICES)
                    serial->reports_drained[index] = false;
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
 * @brief Convierte el contador de tramas inválidas de cada handler en eventos.
 * @return true si todos los eventos de error pudieron publicarse.
 */
bool publish_bad_frame_reports(
    EventQueue *queue,
    NodeSerialContext *serial,
    const uint32_t node_ids[EXPECTED_DEVICES],
    comms_handler_t handlers[EXPECTED_DEVICES],
    const bool enabled[EXPECTED_DEVICES]
)
{
    /* El handler acumula errores; aquí se convierten en eventos del reporte. */
    bool success = true;
    for (size_t i = 0u; i < EXPECTED_DEVICES; ++i) {
        if (!enabled[i])
            continue;
        uint64_t count = comms_take_bad_frame_count(&handlers[i]);
        while (count > 0u) {
            uint32_t batch = count > INT32_MAX ? (uint32_t)INT32_MAX :
                (uint32_t)count;
            Event event = {
                .type = EVENT_SERIAL_BAD_FRAME,
                .source = EVENT_SOURCE_NODE,
                .node_id = node_ids[i],
                .tick = serial->latest_ticks[i],
                .host_time_ns = host_time_ns(),
                .detail = (int32_t)batch
            };
            if (!publish_report_event(queue, &event))
                success = false;
            count -= batch;
        }
    }
    return success;
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
bool pump_node_reports(
    EventQueue *queue,
    NodeSerialContext *serial,
    const uint32_t node_ids[EXPECTED_DEVICES],
    comms_handler_t handlers[EXPECTED_DEVICES],
    const bool enabled[EXPECTED_DEVICES]
)
{
    /* Vaciar los mensajes disponibles sin bloquear al worker por una respuesta. */
    bool success = publish_bad_frame_reports(
        queue, serial, node_ids, handlers, enabled
    );
    for (size_t i = 0u; i < EXPECTED_DEVICES; ++i) {
        /* Cada proceso worker solo inicializa y atiende el handler habilitado. */
        if (!enabled[i])
            continue;
        for (;;) {
            char response[COMMS_MESSAGE_CAPACITY];
            size_t response_size = 0u;
            bool ready = false;
            if (!take_received_message(&handlers[i], response, sizeof(response),
                                      &response_size, &ready)) {
                success = false;
                event_queue_mark_incomplete(queue);
                break;
            }
            if (!ready)
                break;

            if (response_size > 0u && response[response_size - 1u] == '\0')
                --response_size;

            Message message;
            MessageParseResult parsed =
                message_parse(response, response_size, &message);
            if (parsed != MESSAGE_PARSE_OK) {
                if (response_size > 0u && response[0] == '{') {
                    fprintf(stderr,
                        "Mensaje de reporte inválido del nodo %u: %s\n",
                        node_ids[i], message_parse_result_string(parsed));
                    success = false;
                }
                continue;
            }
            if (message.node_id != node_ids[i]) {
                fprintf(stderr,
                    "Mensaje recibido en enlace %u con node_id %u.\n",
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
            uint64_t report_time_ns = worker_monotonic_time_ns();

            event.host_time_ns = host_time_ns();

            record_node_event_tick(serial, &event);
            record_report_watchdog(serial, i, report_time_ns);

            serial->reports_drained[i] = false;

            if (!publish_report_event(queue, &event))
                success = false;
        }

        if (serial->stop_pending[i] &&
            comms_reports_drained_since(
                &handlers[i], serial->stop_heartbeat_baseline[i])) {
            Event stopped = {
                .type = EVENT_NODE_STOPPED,
                .source = EVENT_SOURCE_HOST,
                .node_id = node_ids[i],
                .tick = serial->latest_ticks[i],
                .host_time_ns = host_time_ns()
            };
            if (publish_report_event(queue, &stopped)) {
                serial->reports_drained[i] = true;
                serial->stop_pending[i] = false;
            } else {
                success = false;
            }
        }
    }
    return success;
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
 * @brief Transfiere una estructura completa por un canal de proceso con timeout.
 *
 * @param descriptor Extremo del pipe.
 * @param data Buffer de entrada o salida.
 * @param size Cantidad exacta de bytes por transferir.
 * @param writing true para escribir y false para leer.
 * @param timeout_ms Tiempo máximo de espera.
 * @return true si se transfirieron todos los bytes.
 */
bool transfer_process_message(
    int descriptor,
    void *data,
    size_t size,
    bool writing,
    int timeout_ms
)
{
    unsigned char *bytes = data;
    size_t transferred = 0u;
    /* read/write pueden ser parciales; completar la estructura antes de retornar. */
    uint64_t deadline = monotonic_time_ms() + (uint64_t)timeout_ms;
    while (transferred < size) {
        uint64_t now = monotonic_time_ms();
        if (now >= deadline)
            return false;
        struct pollfd descriptor_event = {
            .fd = descriptor,
            .events = writing ? POLLOUT : POLLIN
        };
        int ready = poll(&descriptor_event, 1u,
                         (int)(deadline - now));
        if (ready < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }
        if (ready == 0 || (descriptor_event.revents & (POLLERR | POLLNVAL)))
            return false;
        ssize_t count = writing
            ? write(descriptor, bytes + transferred, size - transferred)
            : read(descriptor, bytes + transferred, size - transferred);
        if (count < 0) {
            if (errno == EINTR || errno == EAGAIN)
                continue;
            return false;
        }
        if (count == 0)
            return false;
        transferred += (size_t)count;
    }
    return true;
}

/**
 * @brief Envía una orden al proceso dueño de un nodo y espera su resultado.
 *
 * @param context Contexto del proceso coordinador.
 * @param node_index Índice del nodo destino.
 * @param command Orden serializada para el worker.
 * @param reply Resultado devuelto por el worker.
 * @return true si hubo una respuesta válida dentro del timeout.
 */
static bool request_node_worker(
    EventQueue *queue,
    HostCoordinatorContext *context,
    size_t node_index,
    const NodeCommand *command,
    NodeReply *reply
)
{
    if (context == NULL || command == NULL || reply == NULL ||
        node_index >= EXPECTED_DEVICES)
        return false;
    int reply_timeout_ms = command->type == NODE_COMMAND_RECONFIGURE
        ? 70000 : 12000;
    if (!transfer_process_message(context->command_write_fd[node_index],
                                  (void *)command, sizeof(*command), true, 6000) ||
        !transfer_process_message(context->reply_read_fd[node_index],
                                  reply, sizeof(*reply), false,
                                  reply_timeout_ms)) {
        fprintf(stderr, "No hubo respuesta del proceso del nodo %u.\n",
            context->node_ids[node_index]);
        event_queue_mark_incomplete(queue);
        return false;
    }
    return true;
}

/**
 * @brief Detiene los hilos seriales indicados y espera a que terminen.
 * Solo opera sobre handlers locales al proceso que llama esta función.
 */
void stop_handlers(
    comms_handler_t *handlers,
    const bool *handler_started,
    size_t count
)
{
    /* Primero solicitar el cierre a todos; después esperar a cada hilo. */
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
 * @brief Cierra en cada hijo los extremos de pipes que no le pertenecen.
 *
 * El reporter no usa los canales de control. Cada worker conserva únicamente
 * su extremo de lectura de órdenes y su extremo de escritura de respuestas.
 *
 * @param reporter true si la configuración se ejecuta en el proceso reporter.
 * @param worker_index Índice del worker; se ignora en el proceso reporter.
 * @param opaque Contexto del coordinador con todos los descriptores heredados.
 * @return Cero si se cerraron todos los extremos que no se necesitan.
 */
static int prepare_child_pipe_descriptors(
    bool reporter,
    size_t worker_index,
    void *opaque
)
{
    /* Cada hijo conserva solo sus dos pipes; el reporter no conserva ninguno. */
    HostCoordinatorContext *context = opaque;
    if (context == NULL || (!reporter && worker_index >= EXPECTED_DEVICES))
        return 1;

    int retained[3] = {-1, -1, -1};
    /* Mantener abiertos únicamente los extremos que el proceso usará. */
    if (!reporter) {
        retained[0] = context->workers[worker_index].command_read_fd;
        retained[1] = context->workers[worker_index].reply_write_fd;
        retained[2] = context->workers[worker_index].link_status_write_fd;
    }

    bool success = true;
    for (size_t i = 0u; i < EXPECTED_DEVICES * 6u; ++i) {
        int fd = context->pipe_fds[i];
        if (fd < 0 || fd == retained[0] || fd == retained[1] ||
            fd == retained[2])
            continue;
        if (close(fd) != 0) {
            perror("close inherited node pipe");
            success = false;
        }
    }
    return success ? 0 : 1;
}

/**
 * @brief Cierra en el proceso coordinador los extremos usados solo por hijos.
 *
 * El coordinador conserva los extremos que escriben órdenes y leen respuestas.
 *
 * @param opaque Contexto con todos los descriptores de pipes.
 * @return Cero si se cerraron correctamente los extremos no usados.
 */
static int close_parent_pipe_descriptors(void *opaque)
{
    /* El padre no debe mantener abiertos los extremos que pertenecen a hijos. */
    HostCoordinatorContext *context = opaque;
    if (context == NULL)
        return 1;

    bool success = true;
    for (size_t i = 0u; i < EXPECTED_DEVICES; ++i) {
        size_t base = i * 6u;
        const size_t child_only[] = {base, base + 3u, base + 5u};
        for (size_t j = 0u; j < sizeof(child_only) / sizeof(child_only[0]); ++j) {
            int fd = context->pipe_fds[child_only[j]];
            if (fd >= 0 && close(fd) != 0)
                success = false;
            context->pipe_fds[child_only[j]] = -1;
        }
    }
    return success ? 0 : 1;
}

/**
 * @brief Solicita a un worker que almacene una tarea ya admitida.
 *
 * @param queue Cola donde se publican rechazos y eventos del nodo.
 * @param context Contexto IPC del coordinador.
 * @param decision Resultado de asignación y admisión.
 * @param task Definición de la tarea por desplegar.
 * @param reply Resultado del ACK/NACK del nodo.
 * @return true si el worker respondió a la solicitud.
 */
static bool coordinator_deploy_task(
    EventQueue *queue,
    HostCoordinatorContext *context,
    const PlannerDecision *decision,
    const Task *task,
    NodeReply *reply
)
{
    /* La decisión ya tiene nodo destino; convertirla en orden IPC para ese worker. */
    size_t index = 0u;
    while (index < EXPECTED_DEVICES &&
           context->node_ids[index] != decision->node_id)
        ++index;
    if (index == EXPECTED_DEVICES)
        return false;
    NodeCommand command = {
        .type = NODE_COMMAND_DEPLOY,
        .message = {
            .type = MSG_DEPLOY_TASK,
            .node_id = decision->node_id,
            .request_id = context->next_deploy_request_id++,
            .data.deploy_task = {
                .task = *task,
                .cost_ticks = decision->cost_ticks
            }
        }
    };
    return request_node_worker(queue, context, index, &command, reply);
}

/**
 * @brief Planifica y despliega el CSV usando los workers independientes.
 *
 * @param queue Cola IPC de reportes.
 * @param context Configuración, planner y canales de los nodos.
 * @param tasks_path Ruta del CSV configurado.
 * @param capacities Capacidades calibradas en UT por tick.
 * @return true si el lote se procesó y se completó el despliegue.
 */
static bool coordinator_deploy_initial_tasks(
    EventQueue *queue,
    HostCoordinatorContext *context,
    const char *tasks_path,
    const uint32_t capacities[EXPECTED_DEVICES]
)
{
    /* Cargar CSV, planificar con capacidades calibradas y desplegar en orden. */
    TaskList tasks = {0};
    PlannerDecision *decisions = NULL;
    char error[256];
    bool success = false;
    if (!task_parser_load(tasks_path, &tasks, error, sizeof(error))) {
        fprintf(stderr, "No se pudo cargar %s: %s\n", tasks_path, error);
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
    if (!planner_init(&context->planner, context->config.algorithm,
                      context->config.heuristic)) {
        fprintf(stderr, "No se pudo inicializar el planificador.\n");
        goto cleanup;
    }
    /* El Planner usa las capacidades reportadas por cada worker al arrancar. */
    for (size_t i = 0u; i < EXPECTED_DEVICES; ++i) {
        if (!planner_add_node(&context->planner, context->node_ids[i],
                              capacities[i])) {
            fprintf(stderr, "No se pudo registrar el nodo %u.\n",
                context->node_ids[i]);
            goto cleanup;
        }
    }
    if (!planner_plan_batch(&context->planner, tasks.items, tasks.count,
                            decisions)) {
        fprintf(stderr, "No se pudo planificar el lote de tareas.\n");
        goto cleanup;
    }

    context->next_deploy_request_id = FIRST_DEPLOY_REQUEST_ID;
    printf("Planificación: %zu tarea(s) cargada(s) desde %s.\n",
        tasks.count, tasks_path);
    /* Registrar rechazos locales; enviar al nodo solo las tareas admitidas. */
    for (size_t i = 0u; i < tasks.count; ++i) {
        const PlannerDecision *decision = &decisions[i];
        if (!decision->accepted) {
            Event rejected = {
                .type = EVENT_TASK_REJECTED,
                .source = EVENT_SOURCE_HOST,
                .task_id = tasks.items[i].id,
                .detail = decision->reason,
                .task = tasks.items[i],
                .host_time_ns = host_time_ns()
            };
            if (!publish_report_event(queue, &rejected))
                goto cleanup;
            printf("Tarea %" PRIu32 " no admitida: %s.\n",
                tasks.items[i].id, report_reject_reason(decision->reason));
            continue;
        }

        NodeReply reply;
        if (!coordinator_deploy_task(queue, context, decision,
                                     &tasks.items[i], &reply) || !reply.ok) {
            fprintf(stderr, "Falló el despliegue de la tarea %" PRIu32 ".\n",
                decision->task_id);
            goto cleanup;
        }
        Event event = {
            .type = reply.accepted ? EVENT_TASK_ACCEPTED : EVENT_TASK_REJECTED,
            .source = reply.accepted ? EVENT_SOURCE_HOST : EVENT_SOURCE_NODE,
            .node_id = decision->node_id,
            .task_id = decision->task_id,
            .cost_ticks = reply.accepted ? decision->cost_ticks : 0u,
            .detail = reply.accepted ? REJECT_NONE : reply.reason,
            .task = tasks.items[i],
            .host_time_ns = host_time_ns()
        };
        if (!reply.accepted &&
            !planner_remove_task(&context->planner, decision->node_id,
                                 decision->task_id)) {
            fprintf(stderr, "No se pudo revertir la reserva de la tarea.\n");
            goto cleanup;
        }
        if (!publish_report_event(queue, &event))
            goto cleanup;
        printf("Tarea %" PRIu32 " %s en nodo %" PRIu32 ".\n",
            decision->task_id, reply.accepted ? "almacenada" : "rechazada",
            decision->node_id);
    }
    success = true;

cleanup:
    free(decisions);
    task_list_free(&tasks);
    return success;
}

/**
 * @brief Admite manualmente una tarea y solicita su despliegue en ejecución.
 *
 * @param queue Cola IPC de reportes.
 * @param context Estado del coordinador.
 * @param text Definición textual id,P|S,W,T,D,criticidad.
 */
static void coordinator_add_manual_task(
    EventQueue *queue,
    HostCoordinatorContext *context,
    const char *text
)
{
    /* Reservar primero en Planner; confirmar o revertir según ACK/NACK remoto. */
    Task task;
    char error[128];
    if (!manual_task_parse(text, &task, error, sizeof(error))) {
        printf("Tarea manual inválida: %s. Formato: "
               "add id,P|S,W,T,D,criticidad\n", error);
        return;
    }
    ManualTaskRequest request = {0};
    if (!manual_task_submit(&context->planner, &task, &request)) {
        fprintf(stderr, "No se pudo procesar la tarea manual.\n");
        return;
    }
    const PlannerDecision *decision = &request.decision;
    if (!decision->accepted) {
        Event rejected = {
            .type = EVENT_TASK_REJECTED,
            .source = EVENT_SOURCE_HOST,
            .task_id = task.id,
            .detail = decision->reason,
            .task = task,
            .host_time_ns = host_time_ns()
        };
        if (publish_report_event(queue, &rejected))
            printf("Tarea %" PRIu32 " no admitida: %s.\n",
                task.id, report_reject_reason(decision->reason));
        return;
    }

    NodeReply reply;
    if (!coordinator_deploy_task(queue, context, decision, &task, &reply) ||
        !reply.ok) {
        event_queue_mark_incomplete(queue);
        fprintf(stderr, "Resultado incierto al desplegar tarea %" PRIu32 ".\n",
            task.id);
        return;
    }
    if (!reply.accepted) {
        if (!manual_task_cancel(&context->planner, &request))
            event_queue_mark_incomplete(queue);
    } else if (!manual_task_confirm(&request)) {
        event_queue_mark_incomplete(queue);
        fprintf(stderr, "No se pudo confirmar la tarea manual.\n");
        return;
    }
    Event event = {
        .type = reply.accepted ? EVENT_TASK_ACCEPTED : EVENT_TASK_REJECTED,
        .source = reply.accepted ? EVENT_SOURCE_HOST : EVENT_SOURCE_NODE,
        .node_id = decision->node_id,
        .task_id = task.id,
        .cost_ticks = reply.accepted ? decision->cost_ticks : 0u,
        .detail = reply.accepted ? REJECT_NONE : reply.reason,
        .task = task,
        .host_time_ns = host_time_ns()
    };
    if (publish_report_event(queue, &event))
        printf("Tarea %" PRIu32 " %s en nodo %" PRIu32 ".\n",
            task.id, reply.accepted ? "almacenada" : "rechazada",
            decision->node_id);
}

/**
 * @brief Envía START o STOP a un worker y espera el resultado remoto.
 *
 * @param queue Cola IPC para eventos recibidos durante la espera.
 * @param context Contexto de comunicación con los workers.
 * @param type Tipo de orden de control.
 * @param index Índice del nodo.
 * @param request_id Identificador correlativo.
 * @return true si el nodo confirmó la orden.
 */
static bool coordinator_control_node(
    EventQueue *queue,
    HostCoordinatorContext *context,
    MessageType type,
    size_t index,
    uint32_t request_id
)
{
    /* El padre nunca usa el puerto: manda la orden al proceso dueño del nodo. */
    NodeCommand command = {
        .type = type == MSG_START ? NODE_COMMAND_START : NODE_COMMAND_STOP,
        .message = {
            .type = type,
            .node_id = context->node_ids[index],
            .request_id = request_id
        }
    };
    NodeReply reply;
    if (!request_node_worker(queue, context, index, &command, &reply))
        return false;
    if (!reply.ok) {
        fprintf(stderr, "%s no confirmado por nodo %u.\n",
            type == MSG_START ? "START" : "STOP", context->node_ids[index]);
        return false;
    }
    context->node_should_run[index] = type == MSG_START;
    return true;
}

/* Replanifica las tareas del nodo caído y despliega las que quepan en el otro. */
static bool coordinator_failover_node(EventQueue *queue,
                                      HostCoordinatorContext *context,
                                      size_t failed_index,
                                      uint64_t failed_tick)
{
    FailoverResult *result = &context->failovers[failed_index];
    if (!failover_handle_node_failure(&context->planner,
                                      context->node_ids[failed_index], result))
        return false;

    Event failed = {
        .type = EVENT_NODE_FAILED,
        .source = EVENT_SOURCE_HOST,
        .node_id = context->node_ids[failed_index],
        .tick = failed_tick,
        .host_time_ns = host_time_ns()
    };
    if (!publish_report_event(queue, &failed))
        return false;

    bool success = true;
    for (size_t i = 0u; i < result->count; ++i) {
        FailoverEntry *entry = &result->entries[i];
        if (!entry->decision.accepted) {
            Event rejected = {
                .type = EVENT_TASK_REJECTED,
                .source = EVENT_SOURCE_HOST,
                .task_id = entry->task.id,
                .detail = entry->decision.reason,
                .task = entry->task,
                .host_time_ns = host_time_ns()
            };
            if (!publish_report_event(queue, &rejected))
                success = false;
            fprintf(stderr, "Failover: tarea %u sin capacidad disponible (%s).\n",
                entry->task.id,
                report_reject_reason(entry->decision.reason));
            continue;
        }

        NodeReply reply;
        if (!coordinator_deploy_task(queue, context, &entry->decision,
                                     &entry->task, &reply) || !reply.ok) {
            success = false;
            continue;
        }
        if (!reply.accepted) {
            if (!planner_remove_task(&context->planner,
                                     entry->decision.node_id,
                                     entry->task.id))
                success = false;
        }
        Event moved = {
            .type = reply.accepted ? EVENT_TASK_ACCEPTED : EVENT_TASK_REJECTED,
            .source = reply.accepted ? EVENT_SOURCE_HOST : EVENT_SOURCE_NODE,
            .node_id = entry->decision.node_id,
            .task_id = entry->task.id,
            .cost_ticks = reply.accepted ? entry->decision.cost_ticks : 0u,
            .detail = reply.accepted ? REJECT_NONE : reply.reason,
            .task = entry->task,
            .host_time_ns = host_time_ns()
        };
        if (!publish_report_event(queue, &moved))
            success = false;
        printf("Failover: tarea %u %s en nodo %u.\n", entry->task.id,
            reply.accepted ? "reasignada" : "rechazada por el nodo",
            entry->decision.node_id);
    }
    return success;
}

/* Recupera en el nodo reconectado las tareas que no cupieron en el sobreviviente. */
static bool coordinator_restore_unmigrated_tasks(
    EventQueue *queue,
    HostCoordinatorContext *context,
    size_t node_index,
    bool *restored_any
)
{
    FailoverResult *result = &context->failovers[node_index];
    bool success = true;
    *restored_any = false;
    for (size_t i = 0u; i < result->count; ++i) {
        FailoverEntry *entry = &result->entries[i];
        if (entry->decision.accepted)
            continue; /* Ya quedó asignada al nodo sobreviviente. */

        PlannerDecision restored = planner_assign_task(
            &context->planner, &entry->task
        );
        if (!restored.accepted) {
            fprintf(stderr,
                "No se pudo restaurar la tarea %u en el nodo %u (%s).\n",
                entry->task.id, context->node_ids[node_index],
                report_reject_reason(restored.reason));
            success = false;
            continue;
        }

        NodeReply reply;
        bool replied = coordinator_deploy_task(
            queue, context, &restored, &entry->task, &reply
        );
        bool accepted = replied && reply.ok &&
            (reply.accepted || reply.reason == REJECT_DUPLICATE_TASK);
        if (!accepted) {
            (void)planner_remove_task(&context->planner,
                                      restored.node_id, entry->task.id);
            success = false;
        }

        Event event = {
            .type = accepted ? EVENT_TASK_ACCEPTED : EVENT_TASK_REJECTED,
            .source = accepted ? EVENT_SOURCE_HOST : EVENT_SOURCE_NODE,
            .node_id = restored.node_id,
            .task_id = entry->task.id,
            .cost_ticks = accepted ? restored.cost_ticks : 0u,
            .detail = accepted ? REJECT_NONE :
                (replied ? reply.reason : REJECT_INVALID_STATE),
            .task = entry->task,
            .host_time_ns = host_time_ns()
        };
        if (!publish_report_event(queue, &event))
            success = false;
        if (accepted)
            *restored_any = true;
        if (accepted)
            printf("Failover: tarea %u restaurada en nodo %u.\n",
                entry->task.id, restored.node_id);
    }
    return success;
}

/* Aplica al planificador las transiciones reportadas por cada worker serial. */
static bool coordinator_handle_link_status(EventQueue *queue,
                                          HostCoordinatorContext *context,
                                          const NodeLinkStatus *status)
{
    size_t index = 0u;
    while (index < EXPECTED_DEVICES &&
           context->node_ids[index] != status->node_id)
        ++index;
    if (index == EXPECTED_DEVICES)
        return false;

    bool was_online = context->node_online[index];
    context->node_online[index] = status->online;
    if (!status->online && was_online && !context->failover_active[index]) {
        context->failover_active[index] = true;
        printf("Nodo %u desconectado; iniciando failover.\n", status->node_id);
        if (!coordinator_failover_node(queue, context, index, status->tick)) {
            event_queue_mark_incomplete(queue);
            return false;
        }
    } else if (status->online && context->failover_active[index]) {
        context->failover_active[index] = false;
        for (size_t i = 0u; i < context->planner.node_count; ++i) {
            if (context->planner.nodes[i].id == status->node_id)
                context->planner.nodes[i].status = NODE_READY;
        }
        printf("Nodo %u reconectado; disponible para nuevas tareas.\n",
            status->node_id);
        NodeCommand reconfigure = {.type = NODE_COMMAND_RECONFIGURE};
        NodeReply calibration;
        if (!request_node_worker(queue, context, index, &reconfigure,
                                 &calibration) || !calibration.ok ||
            calibration.ut_per_tick == 0u) {
            fprintf(stderr, "No se pudo reconfigurar el nodo %u al reconectar.\n",
                status->node_id);
            event_queue_mark_incomplete(queue);
            return false;
        }
        context->planner.nodes[index].ut_per_tick = calibration.ut_per_tick;

        bool restored_any = false;
        if (!coordinator_restore_unmigrated_tasks(
                queue, context, index, &restored_any)) {
            event_queue_mark_incomplete(queue);
            return false;
        }
        if (restored_any && context->node_should_run[index]) {
            /* STOP normaliza firmware que siguió activo; START vuelve a correrlo. */
            if (!coordinator_control_node(
                    queue, context, MSG_STOP, index,
                    take_control_request_id(&context->next_control_request_id)) ||
                !coordinator_control_node(
                    queue, context, MSG_START, index,
                    take_control_request_id(&context->next_control_request_id))) {
                event_queue_mark_incomplete(queue);
                return false;
            }
            printf("Scheduler del nodo %u reanudado tras la reconexión.\n",
                status->node_id);
        }
    }
    return true;
}

/**
 * @brief Coordina los dos workers, el despliegue y la consola de usuario.
 *
 * Esta función corre en el proceso Host padre: los enlaces seriales y su
 * recepción pertenecen exclusivamente a los procesos workers por nodo.
 *
 * @param queue Cola compartida de reportes.
 * @param opaque Contexto con los dos canales IPC y el planificador.
 * @return Cero ante cierre normal solicitado con q.
 */
static int host_coordinator_run(EventQueue *queue, void *opaque)
{
    /* El padre espera a que ambos workers completen CONFIG y calibración. */
    HostCoordinatorContext *context = opaque;
    /* La capacidad de ambos nodos es requisito para asignar las tareas del CSV. */
    uint32_t capacities[EXPECTED_DEVICES] = {0u, 0u};
    for (size_t i = 0u; i < EXPECTED_DEVICES; ++i) {
        /* El worker notifica al padre su capacidad después de calibrar. */
        NodeReply startup;
        if (!transfer_process_message(context->reply_read_fd[i], &startup,
                                      sizeof(startup), false, 12000) ||
            !startup.ok || startup.ut_per_tick == 0u) {
            fprintf(stderr, "Falló la preparación del proceso del nodo %u.\n",
                context->node_ids[i]);
            event_queue_mark_incomplete(queue);
            return 1;
        }
        capacities[i] = startup.ut_per_tick;
    }
    /* Con ambas capacidades conocidas, planificar y desplegar el lote inicial. */
    if (!coordinator_deploy_initial_tasks(
            queue, context, context->config.tasks_file, capacities))
        return 1;

    context->next_control_request_id = FIRST_CONTROL_REQUEST_ID;
    /* Este buffer arma una línea de consola sin bloquear el procesamiento de la sesión. */
    char line[320];
    size_t used = 0u;
    bool overflow = false;
    puts("Procesos activos: Host coordinador, reporter, worker ESP32 y worker Pico.");
    puts("Comandos: add id,P|S,W,T,D,criticidad, start [1|2], stop [1|2], q.");
    fputs("> ", stdout);
    fflush(stdout);
    /* El coordinador lee la consola; los puertos seriales solo los tocan workers. */
    /* La consola vive en el padre; los workers siguen atendiendo serial en paralelo. */
    while (!event_queue_stop_requested(queue)) {
        struct pollfd input[1u + EXPECTED_DEVICES];
        input[0] = (struct pollfd){.fd = STDIN_FILENO, .events = POLLIN};
        for (size_t i = 0u; i < EXPECTED_DEVICES; ++i)
            input[i + 1u] = (struct pollfd){
                .fd = context->link_status_read_fd[i], .events = POLLIN
            };
        int ready = poll(input, 1u + EXPECTED_DEVICES, 100);
        if (ready < 0) {
            if (errno == EINTR)
                return 0;
            perror("poll stdin");
            event_queue_mark_incomplete(queue);
            return 1;
        }
        if (ready == 0)
            continue;
        for (size_t i = 0u; i < EXPECTED_DEVICES; ++i) {
            if (input[i + 1u].revents & (POLLERR | POLLNVAL | POLLHUP)) {
                fprintf(stderr, "Se cerró el canal de estado del nodo %u.\n",
                    context->node_ids[i]);
                event_queue_mark_incomplete(queue);
                return 1;
            }
            if (!(input[i + 1u].revents & POLLIN))
                continue;
            NodeLinkStatus status;
            ssize_t bytes;
            do {
                bytes = read(context->link_status_read_fd[i], &status,
                             sizeof(status));
            } while (bytes < 0 && errno == EINTR);
            if (bytes != (ssize_t)sizeof(status) ||
                !coordinator_handle_link_status(queue, context, &status)) {
                fprintf(stderr, "No se pudo procesar el estado del nodo %zu.\n",
                    i + 1u);
                event_queue_mark_incomplete(queue);
                return 1;
            }
        }
        if (!(input[0].revents & POLLIN))
            continue;
        if (input[0].revents & (POLLERR | POLLNVAL)) {
            fprintf(stderr, "Error leyendo comandos de control.\n");
            return 1;
        }
        char character;
        ssize_t bytes = read(STDIN_FILENO, &character, 1u);
        if (bytes == 0)
            return 0;
        if (bytes < 0) {
            if (errno == EINTR)
                return 0;
            perror("read stdin");
            return 1;
        }
        if (character != '\n' && character != '\r') {
            if (!overflow && used + 1u < sizeof(line))
                line[used++] = character;
            else
                overflow = true;
            continue;
        }
        if (overflow) {
            puts("Comando demasiado largo; se ignoró la línea.");
            used = 0u;
            overflow = false;
            fputs("> ", stdout);
            fflush(stdout);
            continue;
        }
        line[used] = '\0';
        used = 0u;
        /* q termina el coordinador; report_session inicia el cierre ordenado. */
        if (strcmp(line, "q") == 0)
            return 0;
        if (strncmp(line, "add ", 4u) == 0) {
            /* Las tareas manuales también pasan por planificación y admisión. */
            coordinator_add_manual_task(queue, context, line + 4u);
        } else if (strcmp(line, "start") == 0 ||
                   strcmp(line, "stop") == 0) {
            MessageType type = line[0] == 's' && line[1] == 't' &&
                line[2] == 'a' ? MSG_START : MSG_STOP;
            bool success = true;
            for (size_t i = 0u; i < EXPECTED_DEVICES; ++i)
                if (!coordinator_control_node(queue, context, type, i,
                        take_control_request_id(&context->next_control_request_id)))
                    success = false;
            if (!success)
                event_queue_mark_incomplete(queue);
        } else if (strcmp(line, "start 1") == 0 ||
                   strcmp(line, "start 2") == 0 ||
                   strcmp(line, "stop 1") == 0 ||
                   strcmp(line, "stop 2") == 0) {
            bool starting = line[0] == 's' && line[1] == 't' &&
                line[2] == 'a';
            size_t index = line[strlen(line) - 1u] == '1' ? 0u : 1u;
            if (!coordinator_control_node(
                    queue, context, starting ? MSG_START : MSG_STOP, index,
                    take_control_request_id(&context->next_control_request_id)))
                event_queue_mark_incomplete(queue);
        } else if (line[0] != '\0') {
            puts("Comando inválido. Usa add, start [1|2], stop [1|2] o q.");
        }
        fputs("> ", stdout);
        fflush(stdout);
    }
    return 0;
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
    /* La demo no abre dispositivos; el modo normal necesita ambos puertos. */
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

    /* La ejecución real requiere exactamente un puerto por dispositivo. */
    if (argc != 1 + EXPECTED_DEVICES) {
        fprintf(stderr, "Usage: %s <ESP32 serial port> <Pico serial port>\n", argv[0]);
        return 1;
    }

    /* Cargar configuración y describir los dos nodos que se controlarán. */
    HostCoordinatorContext context = {0};
    char error[256];
    if (!config_load("config/system.conf", &context.config, error, sizeof(error))) {
        fprintf(stderr, "No se pudo cargar config/system.conf: %s\n", error);
        return 1;
    }
    context.devices[0] = argv[1];
    context.devices[1] = argv[2];
    context.node_ids[0] = 1u;
    context.node_ids[1] = 2u;

    /* Crear un canal de órdenes y otro de respuestas por cada worker. */
    int command_pipes[EXPECTED_DEVICES][2];
    int reply_pipes[EXPECTED_DEVICES][2];
    int link_status_pipes[EXPECTED_DEVICES][2];
    for (size_t i = 0u; i < EXPECTED_DEVICES; ++i) {
        command_pipes[i][0] = -1;
        command_pipes[i][1] = -1;
        reply_pipes[i][0] = -1;
        reply_pipes[i][1] = -1;
        link_status_pipes[i][0] = -1;
        link_status_pipes[i][1] = -1;
    }
    for (size_t i = 0u; i < EXPECTED_DEVICES; ++i) {
        /* Los hijos heredarán todos los extremos; su callback cierra los ajenos. */
        if (pipe(command_pipes[i]) != 0 || pipe(reply_pipes[i]) != 0 ||
            pipe(link_status_pipes[i]) != 0) {
            perror("pipe node worker");
            for (size_t j = 0u; j < EXPECTED_DEVICES; ++j) {
                if (command_pipes[j][0] >= 0) close(command_pipes[j][0]);
                if (command_pipes[j][1] >= 0) close(command_pipes[j][1]);
                if (reply_pipes[j][0] >= 0) close(reply_pipes[j][0]);
                if (reply_pipes[j][1] >= 0) close(reply_pipes[j][1]);
                if (link_status_pipes[j][0] >= 0) close(link_status_pipes[j][0]);
                if (link_status_pipes[j][1] >= 0) close(link_status_pipes[j][1]);
            }
            return 1;
        }
        /* El padre conserva write/read; el worker conserva read/write. */
        context.command_write_fd[i] = command_pipes[i][1];
        context.reply_read_fd[i] = reply_pipes[i][0];
        context.pipe_fds[i * 6u] = command_pipes[i][0];
        context.pipe_fds[i * 6u + 1u] = command_pipes[i][1];
        context.pipe_fds[i * 6u + 2u] = reply_pipes[i][0];
        context.pipe_fds[i * 6u + 3u] = reply_pipes[i][1];
        context.pipe_fds[i * 6u + 4u] = link_status_pipes[i][0];
        context.pipe_fds[i * 6u + 5u] = link_status_pipes[i][1];
        context.link_status_read_fd[i] = link_status_pipes[i][0];
        context.workers[i].index = i;
        context.workers[i].command_read_fd = command_pipes[i][0];
        context.workers[i].reply_write_fd = reply_pipes[i][1];
        context.workers[i].link_status_write_fd = link_status_pipes[i][1];
        /* Cada copia de contexto contiene solo los datos seriales de su worker. */
        context.workers[i].serial.config = context.config;
        context.workers[i].serial.devices[i] = context.devices[i];
        context.workers[i].serial.node_ids[i] = context.node_ids[i];
    }

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

    /* La sesión bifurca un worker por nodo y un reporter; main queda coordinando. */
    ReportWorker workers[EXPECTED_DEVICES] = {
        {
            .run = node_worker_run,
            .stop = node_worker_stop,
            .context = &context.workers[0]
        },
        {
            .run = node_worker_run,
            .stop = node_worker_stop,
            .context = &context.workers[1]
        }
    };
    printf("Generando reporte de sesión en %s; use q para finalizar.\n", directory);
    /* La sesión crea los cuatro procesos y administra su cierre/reporte. */
    int result = report_session_run_coordinated(
        directory, workers, EXPECTED_DEVICES, host_coordinator_run,
        prepare_child_pipe_descriptors, close_parent_pipe_descriptors,
        &context
    );
    /* Tras esperar a los hijos, el padre libera los descriptores heredados. */
    for (size_t i = 0u; i < EXPECTED_DEVICES; ++i) {
        close(command_pipes[i][0]);
        close(command_pipes[i][1]);
        close(reply_pipes[i][0]);
        close(reply_pipes[i][1]);
        close(link_status_pipes[i][0]);
        close(link_status_pipes[i][1]);
    }
    if (result == 0){
        printf("Sesión cerrada. Reporte HTML y CSV en %s.\n", directory);
    } else {
        fprintf(stderr, "La sesión terminó con errores; revise el reporte en %s.\n",
            directory);
    }
    return result;
}
