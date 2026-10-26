#define _DEFAULT_SOURCE

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "common/message_parser.h"
#include "host/config.h"
#include "comms/comms.h"

#define EXPECTED_DEVICES 2
#define HANDSHAKE_TIMEOUT_SECONDS 30

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
            if (parsed == MESSAGE_PARSE_OK &&
                reply.type == MSG_ACK &&
                reply.node_id == node_id &&
            reply.request_id == request_id &&
                reply.data.reply.type == MSG_CONFIG) {
                printf("CONFIG confirmado por nodo %u (%s).\n", node_id, device);
                return true;
            }

            if (parsed != MESSAGE_PARSE_OK) {
                fprintf(stderr, "Respuesta no JSON válida de nodo %u (%s): %s\n",
                    node_id, device, message_parse_result_string(parsed));
            } else {
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

static void stop_handlers(comms_handler_t *handlers, size_t count)
{
    for (size_t i = 0u; i < count; ++i)
        comms_stop_handler(&handlers[i]);

    for (size_t i = 0u; i < count; ++i) {
        for (;;) {
            pthread_mutex_lock(&handlers[i].mutex);
            bool running = handlers[i].running;
            pthread_mutex_unlock(&handlers[i].mutex);
            if (!running)
                break;
            struct timespec pause = {.tv_nsec = 10000000L};
            nanosleep(&pause, NULL);
        }
    }
}

int main(int argc, char *argv[])
{
    if (argc != 1 + EXPECTED_DEVICES) {
        fprintf(stderr, "Usage: %s <ESP32 serial port> <Pico serial port>\n", argv[0]);
        return 1;
    }

    HostConfig config;
    char error[256];
    if (!config_load("config/system.conf", &config, error, sizeof(error))) {
        fprintf(stderr, "No se pudo cargar config/system.conf: %s\n", error);
        return 1;
    }

    const char *devices[EXPECTED_DEVICES] = {argv[1], argv[2]};
    const uint32_t node_ids[EXPECTED_DEVICES] = {1u, 2u};
    comms_handler_t handlers[EXPECTED_DEVICES] = {0};
    char sent[EXPECTED_DEVICES][COMMS_MESSAGE_CAPACITY];
    char received[EXPECTED_DEVICES][COMMS_MESSAGE_CAPACITY];
    bool setup_ok[EXPECTED_DEVICES] = {false, false};
    bool acked[EXPECTED_DEVICES] = {false, false};

    for (size_t i = 0u; i < EXPECTED_DEVICES; ++i) {
        if (comms_init_handler((int)i, &handlers[i], devices[i], 0u,
                               config.tick_ms, config.missed_reports_limit)
            != COMMS_OK) {
            fprintf(stderr, "No se pudo inicializar el enlace %s.\n", devices[i]);
            stop_handlers(handlers, i);
            return 1;
        }
        handlers[i].sent.content = sent[i];
        handlers[i].recv.content = received[i];

        Message request = {
            .type = MSG_CONFIG,
            .node_id = node_ids[i],
            .request_id = 1u,
            .data.config = {
                .algorithm = config.algorithm == SCHED_EDF
                    ? MESSAGE_SCHED_EDF : MESSAGE_SCHED_RMS,
                .tick_ms = config.tick_ms,
                .report_every_ticks = config.report_every_ticks
            }
        };
        size_t json_length = 0u;
        MessageSerializeResult serialized = message_serialize(
            &request, sent[i], sizeof(sent[i]), &json_length
        );
        if (serialized != MESSAGE_SERIALIZE_OK) {
            fprintf(stderr, "No se pudo serializar CONFIG para nodo %u (error %d).\n",
                node_ids[i], (int)serialized);
            stop_handlers(handlers, i + 1u);
            return 1;
        }

        if (comms_request_send(&handlers[i], sent[i], 0u) != COMMS_OK) {
            fprintf(stderr, "No se pudo preparar CONFIG para nodo %u.\n", node_ids[i]);
            stop_handlers(handlers, i + 1u);
            return 1;
        }
        printf("Enviando CONFIG a nodo %u por %s (%zu bytes JSON).\n",
            node_ids[i], devices[i], json_length);
        setup_ok[i] = comms_setup_handler(&handlers[i]) == COMMS_OK;
        if (!setup_ok[i]) {
            fprintf(stderr, "No se pudo iniciar el enlace %s.\n", devices[i]);
            stop_handlers(handlers, i + 1u);
            return 1;
        }
    }

    for (size_t i = 0u; i < EXPECTED_DEVICES; ++i)
        acked[i] = wait_for_ack(&handlers[i], node_ids[i], 1u, devices[i]);

    stop_handlers(handlers, EXPECTED_DEVICES);
    if (!acked[0] || !acked[1]) {
        fprintf(stderr, "Intercambio CONFIG incompleto; revise conexiones y firmware.\n");
        return 1;
    }

    puts("CONFIG/ACK completado en ambos dispositivos.");
    return 0;
}
