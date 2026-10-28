#define _DEFAULT_SOURCE
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdint.h>
#include "host/workers/node_worker.h"
#include "common/message_parser.h"
#include "timing/timing.h"

#define RECONFIGURE_ATTEMPTS 3u
#define RECONFIGURE_READY_TIMEOUT_MS 10000u

/* Espera a que el handler haya reconectado y pueda aceptar otro mensaje. */
static bool wait_until_send_ready(comms_handler_t *handler)
{
    uint64_t deadline = monotonic_time_ms() + RECONFIGURE_READY_TIMEOUT_MS;
    while (monotonic_time_ms() < deadline) {
        pthread_mutex_lock(&handler->mutex);
        bool online = handler->online;
        bool send_free = !handler->sent.set;
        pthread_mutex_unlock(&handler->mutex);
        if (online && send_free)
            return true;
        struct timespec pause = {.tv_nsec = 100000000L};
        nanosleep(&pause, NULL);
    }
    return false;
}

/* Notifica al coordinador los cambios de enlace después de la conexión inicial. */
static bool publish_link_transition(NodeWorkerContext *context,
                                    comms_handler_t *handler,
                                    uint32_t node_id)
{
    pthread_mutex_lock(&handler->mutex);
    bool online = handler->online;
    pthread_mutex_unlock(&handler->mutex);

    if (!context->link_status_known) {
        if (!online)
            return true;
        context->link_status_known = true;
        context->last_link_online = false;
    }
    if (online == context->last_link_online)
        return true;

    NodeLinkStatus status = {
        .node_id = node_id,
        .tick = context->serial.latest_ticks[context->index],
        .online = online
    };
    const unsigned char *bytes = (const unsigned char *)&status;
    size_t written = 0u;
    while (written < sizeof(status)) {
        ssize_t count = write(context->link_status_write_fd, bytes + written,
                              sizeof(status) - written);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            return false;
        written += (size_t)count;
    }
    context->last_link_online = online;
    return true;
}

void report_watchdog_start(
    NodeSerialContext *session,
    size_t index
)
{
    session->report_window_start_ns[index] =
        worker_monotonic_time_ns();

    session->report_count[index] = 0u;
    session->report_watchdog_active[index] = true;
    session->report_node_online[index] = true;
}

void report_watchdog_stop(
    NodeSerialContext *session,
    size_t index
)
{
    session->report_watchdog_active[index] = false;
    session->report_count[index] = 0u;
    session->report_node_online[index] = false;
}

/*
 * Call only after a valid MSG_EVENT or MSG_TICK_REPORT has been parsed.
 */
void record_report_watchdog(
    NodeSerialContext *session,
    size_t index,
    uint64_t timestamp_ns
)
{
    if (!session->report_watchdog_active[index])
        return;

    if (session->report_count[index] < UINT32_MAX)
        ++session->report_count[index];

    /*
     * A valid report is evidence that the node has resumed activity.
     * Do not modify comms_handler_t::online; the handler owns serial state.
     */
    if (!session->report_node_online[index]) {
        session->report_node_online[index] = true;

        printf(
            "Nodo %d (%s) volvió a enviar reportes.\n",
            session->node_ids[index],
            session->devices[index]
        );

        /*
         * Start a fresh window so old silence does not immediately
         * trigger another offline transition.
         */
        session->report_window_start_ns[index] = timestamp_ns;
        session->report_count[index] = 1u;
    }
}

/*
 * Returns true when the watchdog was evaluated successfully.
 * A low report count changes logical state, not serial-handler state.
 */
bool check_report_watchdog(
    NodeSerialContext *session,
    size_t index
)
{
    if (!session->report_watchdog_active[index])
        return true;

    const uint64_t now_ns = worker_monotonic_time_ns();
    const uint64_t window_ns = report_watchdog_window_ns(session);

    if (now_ns == 0u || window_ns == 0u)
        return true;

    const uint64_t start_ns =
        session->report_window_start_ns[index];

    if (now_ns < start_ns || now_ns - start_ns < window_ns)
        return true;

    const uint32_t reports = session->report_count[index];
    const bool was_online = session->report_node_online[index];

    if (reports < REPORT_WATCHDOG_MIN_REPORTS) {
        session->report_node_online[index] = false;

        if (was_online) {
            fprintf(
                stderr,
                "Nodo %d (%s) sin actividad suficiente: %d reportes en %llu ms. El handler serial conserva el control del enlace.\n",
                session->node_ids[index],
                session->devices[index],
                reports,
                window_ns / 1000000ull
            );
        }
    } else {
        session->report_node_online[index] = true;
    }

    /*
     * Start a new window. Setting the start to now also prevents a long
     * worker pause from counting the same reports in several windows.
     */
    session->report_window_start_ns[index] = now_ns;
    session->report_count[index] = 0u;

    return true;
}

/**
 * @brief Configura un nodo y luego atiende órdenes del coordinador.
 * @param queue Cola compartida donde se publican eventos para el reporter.
 * @param opaque NodeWorkerContext con índice, puerto y extremos de pipes.
 * @return Cero si el worker sale de forma normal; distinto de cero ante error.
 */
int node_worker_run(EventQueue *queue, void *opaque)
{
    /* El mismo callback sirve para ambos procesos; el índice selecciona el nodo. */
    NodeWorkerContext *context = opaque;
    NodeSerialContext *session = &context->serial;
    size_t index = context->index;
    bool enabled[EXPECTED_DEVICES] = {false, false};
    enabled[index] = true;

    /* 1. Abrir el puerto serial que pertenece a este proceso. */
    comms_handler_t *handler = &session->handlers[index];
    if (comms_init_handler((int)index, handler, session->devices[index], 0u,
                           session->config.connection_timeout_ms) != COMMS_OK) {
        fprintf(stderr, "No se pudo inicializar el enlace %s.\n",
            session->devices[index]);
        return 1;
    }
    session->initialized[index] = true;
    handler->sent.content = session->sent[index];

    /* 2. Enviar la configuración del Host y esperar confirmación del firmware. */
    Message config = {
        .type = MSG_CONFIG,
        .node_id = session->node_ids[index],
        .request_id = 1u,
        .data.config = {
            .algorithm = session->config.algorithm == SCHED_EDF
                ? MESSAGE_SCHED_EDF : MESSAGE_SCHED_RMS,
            .tick_ms = session->config.tick_ms,
            .report_every_ticks = session->config.report_every_ticks
        }
    };

   if (comms_setup_handler(handler) != COMMS_OK) {
        fprintf(stderr, "No se pudo iniciar el enlace %s.\n",
                session->devices[index]);
        return 1;
    }

    session->handler_started[index] = true;

    if (!wait_until_send_ready(handler)) {
        fprintf(stderr, "El enlace %s no está listo para CONFIG.\n",
                session->devices[index]);
        return 1;
    }

    uint64_t send_time_ns = worker_monotonic_time_ns();
    uint64_t rtt_ns = 0u;

    if (!send_message(handler, session->sent[index], &config,
                    session->node_ids[index], "CONFIG")) {
        return 1;
    }

    if (!wait_for_ack(handler, session->node_ids[index], 1u,
                    session->devices[index],
                    &rtt_ns)) {
        return 1;
    }

    session->config_send_time_ns[index] = send_time_ns;
    session->config_round_trip_ns[index] = rtt_ns;
    session->config_ack_time_ns[index] = send_time_ns + rtt_ns;

    /* 3. Medir capacidad; el coordinador la necesita antes de planificar. */
    Message calibration = {
        .type = MSG_CALIBRATE_REQUEST,
        .node_id = session->node_ids[index],
        .request_id = CALIBRATION_REQUEST_ID,
        .data.sample_ticks = CALIBRATION_DEFAULT_SAMPLES
    };
    uint32_t ut_per_tick = 0u;
    if (!send_message(handler, session->sent[index], &calibration,
                      session->node_ids[index], "CALIBRATE_REQUEST") ||
        !wait_for_calibration(handler, session->node_ids[index],
                              CALIBRATION_REQUEST_ID,
                              session->devices[index], &ut_per_tick))
        return 1;
    session->ut_per_tick[index] = ut_per_tick;

    /* 4. Registrar la calibración y contestar al coordinador por el pipe. */
    Event calibrated = {
        .type = EVENT_NODE_CALIBRATED,
        .source = EVENT_SOURCE_HOST,
        .node_id = session->node_ids[index],
        .ut_per_tick = ut_per_tick,
        .host_time_ns = host_time_ns()
    };
    if (!publish_report_event(queue, &calibrated))
        return 1;
    NodeReply startup = {.ok = true, .ut_per_tick = ut_per_tick};
    if (!transfer_process_message(context->reply_write_fd, &startup,
                                  sizeof(startup), true, 2000))
        return 1;

    /* 5. Atender reportes seriales y órdenes IPC hasta cerrar la sesión. */
    while (!event_queue_stop_requested(queue)) {
        /* Los reportes del dispositivo van a la cola compartida del reporter. */
        if (!pump_node_reports(
            queue,
            session,
            session->node_ids,
            session->handlers,
            enabled)) {
            return 1;
        }
        /*
        * This executes even when pump_node_reports receives no reports.
        * The watchdog must not terminate the worker for a report timeout.
        */
        if (session->running[index] &&
            !check_report_watchdog(session, index)) {
            fprintf(stderr,
                    "Error evaluando watchdog del nodo %d\n",
                    session->node_ids[index]);
        }

        if (!publish_link_transition(context, handler,
                                    session->node_ids[index])) {
            return 1;
        }

        /* poll evita bloquear la lectura de reportes esperando una orden. */
        struct pollfd command_event = {
            .fd = context->command_read_fd,
            .events = POLLIN
        };

        int ready = poll(&command_event, 1u, 20);

        if (ready < 0) {
            if (errno == EINTR)
                continue;

            perror("poll node command");
            return 1;
        }

        if (ready == 0)
            continue;

        if (command_event.revents & (POLLERR | POLLNVAL | POLLHUP))
            return 1;

        if (!publish_link_transition(
            context, handler,
            session->node_ids[index])
        ) return 1;

        /* Cada orden se recibe completa; la respuesta vuelve por el pipe inverso. */
        NodeCommand command;
        if (!transfer_process_message(context->command_read_fd, &command,
                                      sizeof(command), false, 1000))
            return 1;
        NodeReply reply = {.reason = REJECT_INVALID_STATE};
        if (command.type == NODE_COMMAND_RECONFIGURE) {
            /* Reaplicar CONFIG/calibración; los cortes breves se recuperan aquí. */
            Message request = {
                .type = MSG_CONFIG,
                .node_id = session->node_ids[index],
                .request_id = 1u,
                .data.config = {
                    .algorithm = session->config.algorithm == SCHED_EDF
                        ? MESSAGE_SCHED_EDF : MESSAGE_SCHED_RMS,
                    .tick_ms = session->config.tick_ms,
                    .report_every_ticks = session->config.report_every_ticks
                }
            };
            bool config_accepted = false;
            RejectReason config_reason = REJECT_NONE;
            bool config_reply_received = false;

            for (unsigned int attempt = 1u;
                attempt <= RECONFIGURE_ATTEMPTS && !config_reply_received;
                ++attempt) {

                if (!wait_until_send_ready(handler))
                    continue;

                uint64_t attempt_send_ns = worker_monotonic_time_ns();
                uint64_t attempt_rtt_ns = 0u;

                if (!send_message(handler, session->sent[index], &request,
                                request.node_id, "CONFIG")) {
                    continue;
                }

                config_reply_received = wait_for_config_reply(
                    queue, session, index, handler,
                    request.node_id, request.request_id,
                    session->devices[index],
                    attempt_send_ns, &attempt_rtt_ns,
                    &config_accepted, &config_reason
                );

                if (config_reply_received) {
                    session->config_send_time_ns[index] = attempt_send_ns;
                    session->config_round_trip_ns[index] = attempt_rtt_ns;
                    session->config_ack_time_ns[index] =
                        attempt_send_ns + attempt_rtt_ns;
                }

                if (!config_reply_received && attempt < RECONFIGURE_ATTEMPTS) {
                    fprintf(stderr,
                        "Reintentando CONFIG para nodo %d (%u/%u).\n",
                        request.node_id, attempt + 1u, RECONFIGURE_ATTEMPTS);
                }
            }

            if (config_reply_received && config_accepted) {
                /* Un CONFIG nuevo implica que el MCU reinició sus ticks. */
                session->tick_offset[index] =
                    session->latest_ticks[index] == UINT64_MAX
                    ? UINT64_MAX : session->latest_ticks[index] + 1u;
                Message calibration = {
                    .type = MSG_CALIBRATE_REQUEST,
                    .node_id = request.node_id,
                    .request_id = CALIBRATION_REQUEST_ID,
                    .data.sample_ticks = CALIBRATION_DEFAULT_SAMPLES
                };
                for (unsigned int attempt = 1u;
                     attempt <= RECONFIGURE_ATTEMPTS && !reply.ok;
                     ++attempt) {
                    uint32_t capacity = 0u;
                    if (wait_until_send_ready(handler) &&
                        send_message(handler, session->sent[index],
                                     &calibration, request.node_id,
                                     "CALIBRATE_REQUEST") &&
                        wait_for_calibration(handler, request.node_id,
                            calibration.request_id, session->devices[index],
                            &capacity)) {
                        session->ut_per_tick[index] = capacity;
                        reply.ok = true;
                        reply.ut_per_tick = capacity;
                    } else if (attempt < RECONFIGURE_ATTEMPTS) {
                        fprintf(stderr,
                            "Reintentando calibración para nodo %u (%u/%u).\n",
                            request.node_id, attempt + 1u,
                            RECONFIGURE_ATTEMPTS);
                    }
                }
            } else if (config_reply_received &&
                       config_reason == REJECT_INVALID_STATE &&
                       session->ut_per_tick[index] > 0u) {
                /* El firmware conservó su configuración y calibración. */
                reply.ok = true;
                reply.ut_per_tick = session->ut_per_tick[index];
            }
            if (reply.ok) {
                Event calibrated = {
                    .type = EVENT_NODE_CALIBRATED,
                    .source = EVENT_SOURCE_HOST,
                    .node_id = request.node_id,
                    .ut_per_tick = reply.ut_per_tick,
                    .host_time_ns = host_time_ns()
                };
                reply.ok = publish_report_event(queue, &calibrated);
            }
        } else if (command.type == NODE_COMMAND_DEPLOY) {
            /* El coordinador ya eligió el nodo; aquí solo se reenvía al firmware. */
            Message *request = &command.message;
            if (request->type == MSG_DEPLOY_TASK &&
                request->node_id == session->node_ids[index] &&
                send_message(handler, session->sent[index], request,
                             request->node_id, "DEPLOY_TASK")) {
                reply.ok = wait_for_task_deploy(
                    queue, session, handler, request->node_id, request->request_id,
                    session->devices[index], &reply.accepted, &reply.reason
                );
            }
        } else if (command.type == NODE_COMMAND_START ||
                   command.type == NODE_COMMAND_STOP) {
            /* START/STOP actualizan el estado local tras recibir ACK/NACK. */
            Message *request = &command.message;
            MessageType type = command.type == NODE_COMMAND_START
                ? MSG_START : MSG_STOP;
            if (request->type == type &&
                request->node_id == session->node_ids[index] &&
                send_message(handler, session->sent[index], request,
                             request->node_id,
                             type == MSG_START ? "START" : "STOP")) {
                uint64_t heartbeat_baseline =
                    comms_heartbeat_count(handler);
                reply.ok = wait_for_control_reply(
                    queue, session, handler, request->node_id, request->request_id,
                    type, session->devices[index], HANDSHAKE_TIMEOUT_SECONDS
                );
                if (reply.ok) {
                    session->running[index] = type == MSG_START;
                    if (type == MSG_START) {
                        session->started_once[index] = true;
                        report_watchdog_start(session, index);

                        Event started = {
                            .type = EVENT_NODE_STARTED,
                            .source = EVENT_SOURCE_HOST,
                            .node_id = request->node_id,
                            .tick = session->latest_ticks[index],
                            .host_time_ns = host_time_ns()
                        };

                        reply.ok = publish_report_event(queue, &started);
                    } else {
                        report_watchdog_stop(session, index);
                        /* STOP también debe esperar los reportes finales del nodo. */
                        session->reports_drained[index] = false;
                        uint64_t start = monotonic_time_ms();
                        while (!comms_reports_drained_since(
                                   handler, heartbeat_baseline) &&
                               monotonic_time_ms() - start < 2000u) {
                            if (!pump_node_reports(
                                    queue, session, session->node_ids,
                                    session->handlers, enabled)) {
                                reply.ok = false;
                                break;
                            }
                            struct timespec pause = {.tv_nsec = 10000000L};
                            nanosleep(&pause, NULL);
                        }
                        if (!comms_reports_drained_since(
                                handler, heartbeat_baseline)) {
                            fprintf(stderr,
                                "No se confirmó el drenado del nodo %u.\n",
                                request->node_id);
                            reply.ok = false;
                        } else {
                            Event stopped = {
                                .type = EVENT_NODE_STOPPED,
                                .source = EVENT_SOURCE_HOST,
                                .node_id = request->node_id,
                                .tick = session->latest_ticks[index],
                                .host_time_ns = host_time_ns()
                            };
                            reply.ok = publish_report_event(queue, &stopped);
                        }
                    }
                }
            }
        }
        if (!transfer_process_message(context->reply_write_fd, &reply,
                                      sizeof(reply), true, 2000))
            return 1;
    }
    return 0;
}


/**
 * @brief Detiene el nodo si hace falta y libera su handler serial.
 * @param queue Cola compartida para publicar los eventos finales.
 * @param opaque El mismo contexto que recibió node_worker_run.
 * @return Cero si el cierre y la publicación final terminaron correctamente.
 */
int node_worker_stop(EventQueue *queue, void *opaque)
{
    /* La sesión llama este callback después de node_worker_run, incluso si falló. */
    NodeWorkerContext *context = opaque;
    NodeSerialContext *session = &context->serial;
    size_t index = context->index;
    if (!session->handler_started[index])
        return session->initialized[index] ? 1 : 0;

    bool success = true;
    comms_handler_t *handler = &session->handlers[index];
    if (session->running[index]) {
        /* Si aún ejecuta tareas, pedir STOP antes de cerrar el puerto. */
        uint32_t request_id = FIRST_CONTROL_REQUEST_ID;
        uint64_t heartbeat_baseline = comms_heartbeat_count(handler);
        Message stop = {
            .type = MSG_STOP,
            .node_id = session->node_ids[index],
            .request_id = request_id
        };
        if (!send_message(handler, session->sent[index], &stop,
                          stop.node_id, "STOP") ||
            !wait_for_control_reply(queue, session, handler, stop.node_id, request_id,
                                    MSG_STOP, session->devices[index], 1u)) {
            success = false;
        } else {
            session->running[index] = false;
            bool enabled[EXPECTED_DEVICES] = {false, false};
            enabled[index] = true;
            uint64_t start = monotonic_time_ms();
            while (!comms_reports_drained_since(handler, heartbeat_baseline) &&
                   monotonic_time_ms() - start < 2000u) {
                if (!pump_node_reports(queue, session, session->node_ids,
                                       session->handlers, enabled)) {
                    success = false;
                    break;
                }
                struct timespec pause = {.tv_nsec = 10000000L};
                nanosleep(&pause, NULL);
            }
            if (!comms_reports_drained_since(handler, heartbeat_baseline)) {
                fprintf(stderr, "No se drenaron los reportes del nodo %u.\n",
                    session->node_ids[index]);
                success = false;
            } else {
                Event stopped = {
                    .type = EVENT_NODE_STOPPED,
                    .source = EVENT_SOURCE_HOST,
                    .node_id = session->node_ids[index],
                    .tick = session->latest_ticks[index],
                    .host_time_ns = host_time_ns()
                };
                if (!publish_report_event(queue, &stopped))
                    success = false;
            }
        }
    }
    /* Cerrar el hilo serial y publicar al reporter los errores acumulados. */
    bool started[EXPECTED_DEVICES] = {false, false};
    started[index] = session->handler_started[index];
    stop_handlers(session->handlers, started, EXPECTED_DEVICES);
    if (!publish_bad_frame_reports(
            queue, session, session->node_ids, session->handlers, started))
        success = false;
    return success ? 0 : 1;
}

