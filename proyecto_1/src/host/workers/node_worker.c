#define _DEFAULT_SOURCE
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "host/workers/node_worker.h"
#include "common/message_parser.h"
#include "timing/timing.h"

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
    SerialReportSession *session = &context->serial;
    size_t index = context->index;
    bool enabled[EXPECTED_DEVICES] = {false, false};
    enabled[index] = true;
    memset(latest_node_ticks, 0, sizeof(latest_node_ticks));
    memset(node_stop_heartbeat_baseline, 0,
           sizeof(node_stop_heartbeat_baseline));
    memset(node_started_once, 0, sizeof(node_started_once));
    memset(node_reports_drained, 0, sizeof(node_reports_drained));
    memset(node_stop_pending, 0, sizeof(node_stop_pending));

    /* 1. Abrir el puerto serial que pertenece a este proceso. */
    comms_handler_t *handler = &session->handlers[index];
    if (comms_init_handler((int)index, handler, session->devices[index], 0u,
                           session->config.connection_timeout_ms) != COMMS_OK) {
        fprintf(stderr, "No se pudo inicializar el enlace %s.\n",
            session->devices[index]);
        return 1;
    }
    session->initialized[index] = true;
    session->initialized_count = 1u;
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
    if (!send_message(handler, session->sent[index], &config,
                      session->node_ids[index], "CONFIG") ||
        comms_setup_handler(handler) != COMMS_OK) {
        fprintf(stderr, "No se pudo iniciar el enlace %s.\n",
            session->devices[index]);
        return 1;
    }
    session->handler_started[index] = true;
    if (!wait_for_ack(handler, session->node_ids[index], 1u,
                      session->devices[index]))
        return 1;

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
        if (!pump_node_reports(queue, session->node_ids, session->handlers,
                               enabled))
            return 1;
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

        /* Cada orden se recibe completa; la respuesta vuelve por el pipe inverso. */
        NodeCommand command;
        if (!transfer_process_message(context->command_read_fd, &command,
                                      sizeof(command), false, 1000))
            return 1;
        NodeReply reply = {.reason = REJECT_INVALID_STATE};
        if (command.type == NODE_COMMAND_DEPLOY) {
            /* El coordinador ya eligió el nodo; aquí solo se reenvía al firmware. */
            Message *request = &command.message;
            if (request->type == MSG_DEPLOY_TASK &&
                request->node_id == session->node_ids[index] &&
                send_message(handler, session->sent[index], request,
                             request->node_id, "DEPLOY_TASK")) {
                reply.ok = wait_for_task_deploy(
                    queue, handler, request->node_id, request->request_id,
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
                    queue, handler, request->node_id, request->request_id,
                    type, session->devices[index], HANDSHAKE_TIMEOUT_SECONDS
                );
                if (reply.ok) {
                    session->running[index] = type == MSG_START;
                    if (type == MSG_START) {
                        node_started_once[index] = true;
                        Event started = {
                            .type = EVENT_NODE_STARTED,
                            .source = EVENT_SOURCE_HOST,
                            .node_id = request->node_id,
                            .tick = latest_node_ticks[index],
                            .host_time_ns = host_time_ns()
                        };
                        reply.ok = publish_report_event(queue, &started);
                    } else {
                        /* STOP también debe esperar los reportes finales del nodo. */
                        node_reports_drained[index] = false;
                        uint64_t start = monotonic_time_ms();
                        while (!comms_reports_drained_since(
                                   handler, heartbeat_baseline) &&
                               monotonic_time_ms() - start < 2000u) {
                            if (!pump_node_reports(
                                    queue, session->node_ids,
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
                                .tick = latest_node_ticks[index],
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
    SerialReportSession *session = &context->serial;
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
            !wait_for_control_reply(queue, handler, stop.node_id, request_id,
                                    MSG_STOP, session->devices[index], 1u)) {
            success = false;
        } else {
            session->running[index] = false;
            bool enabled[EXPECTED_DEVICES] = {false, false};
            enabled[index] = true;
            uint64_t start = monotonic_time_ms();
            while (!comms_reports_drained_since(handler, heartbeat_baseline) &&
                   monotonic_time_ms() - start < 2000u) {
                if (!pump_node_reports(queue, session->node_ids,
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
                    .tick = latest_node_ticks[index],
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
            queue, session->node_ids, session->handlers, started))
        success = false;
    return success ? 0 : 1;
}

