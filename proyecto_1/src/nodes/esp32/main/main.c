#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "driver/uart.h"
#include "esp_log.h"

#include "common/protocol.h"
#include "common/message_parser.h"
#include "common/messages.h"
#include "common/task_store.h"
#include "calibrator.h"
#include "serial/esp32.h"
#include "serial/serial.h"
#include "gpio/gpio.h"

#define LED0 26
#define LED1 25
#define LED2 33
#define LED3 32

#define UART_PORT   UART_NUM_0

#define NODE_ID 1u
#define MESSAGE_LEN (PROTOCOL_PAYLOAD_BYTES + 1u)

static uint32_t configured_tick_ms;
/* Tabla temporal: los despliegues aceptados desaparecen al reiniciar el MCU. */
static TaskStore deployed_tasks;

static bool serialize_reply(
    const Message *request,
    MessageType type,
    RejectReason reason,
    uint32_t ut_per_tick,
    char *response,
    size_t response_capacity,
    uint32_t *response_length
)
{
    Message reply = {
        .type = type,
        .node_id = NODE_ID,
        .request_id = request->request_id
    };
    if (type == MSG_ACK) {
        reply.data.reply.type = request->type;
    } else {
        reply.data.reply.type = request->type;
        reply.data.reply.reason = reason;
    }
    if (type == MSG_CALIBRATE_RESPONSE)
        reply.data.ut_per_tick = ut_per_tick;

    size_t json_length = 0u;
    if (message_serialize(&reply, response, response_capacity,
                          &json_length) != MESSAGE_SERIALIZE_OK) {
        return false;
    }
    *response_length = (uint32_t)json_length;
    return true;
}

static bool calibrate(
    uint32_t sample_ticks,
    uint32_t *ut_per_tick
)
{
    if (configured_tick_ms == 0u ||
        configured_tick_ms > UINT32_MAX / 1000u ||
        sample_ticks == 0u || sample_ticks > CALIBRATION_MAX_SAMPLES) {
        return false;
    }

    uint64_t total_units = 0u;
    for (uint32_t sample = 0u; sample < sample_ticks; ++sample) {
        CalibrationResult result;
        if (!calibrator_run(configured_tick_ms * 1000u, 20u, &result))
            return false;
        total_units += result.units_per_tick;
    }

    uint64_t average = total_units / sample_ticks;
    if (average == 0u || average > UINT32_MAX)
        return false;
    *ut_per_tick = (uint32_t)average;
    return true;
}

/* Prepara CONFIG/ACK JSON y conserva el protocolo de prueba PING/MARCO. */
static bool prepare_response(
    const protocol_frame_t *received,
    char *response,
    size_t response_capacity,
    uint32_t *response_length
)
{
    char request[MESSAGE_LEN];
    size_t request_length = received->length;

    if (request_length > PROTOCOL_PAYLOAD_BYTES)
        return false;
    /* El transporte histórico incluye NUL; el parser recibe la longitud JSON exacta. */
    memcpy(request, received->payload, request_length);
    request[request_length] = '\0';
    if (request_length > 0u && request[request_length - 1u] == '\0')
        --request_length;

    Message incoming;
    if (message_parse(request, request_length, &incoming) == MESSAGE_PARSE_OK) {
        if (incoming.node_id != NODE_ID)
            return false;

        if (incoming.type == MSG_CONFIG) {
            configured_tick_ms = incoming.data.config.tick_ms;
            return serialize_reply(&incoming, MSG_ACK, REJECT_NONE, 0u,
                response, response_capacity, response_length);
        }

        if (incoming.type == MSG_CALIBRATE_REQUEST) {
            uint32_t ut_per_tick = 0u;
            if (calibrate(incoming.data.sample_ticks, &ut_per_tick)) {
                return serialize_reply(&incoming, MSG_CALIBRATE_RESPONSE,
                    REJECT_NONE, ut_per_tick, response, response_capacity,
                    response_length);
            }
            return serialize_reply(&incoming, MSG_NACK,
                REJECT_CALIBRATION_FAILED, 0u, response, response_capacity,
                response_length);
        }

        if (incoming.type == MSG_DEPLOY_TASK) {
            /*
             * El Host ya hizo la prueba de admisión. Este paso valida límites
             * locales, guarda la tarea y confirma el almacenamiento con ACK.
             */
            RejectReason reason = task_store_add(
                &deployed_tasks,
                &incoming.data.deploy_task.task,
                incoming.data.deploy_task.cost_ticks
            );
            MessageType reply_type = reason == REJECT_NONE ? MSG_ACK : MSG_NACK;
            return serialize_reply(&incoming, reply_type, reason, 0u,
                response, response_capacity, response_length);
        }

        return false;
    }

    const char *legacy = "ESP32 didn't understand message";
    if (request_length == 4u && memcmp(request, "PING", 4u) == 0)
        legacy = "PONG from ESP32";
    else if (request_length == 5u && memcmp(request, "MARCO", 5u) == 0 &&
             received->relevance > 0u)
        legacy = "POLO from ESP32";

    size_t legacy_length = strlen(legacy) + 1u;
    if (legacy_length > response_capacity)
        return false;
    memcpy(response, legacy, legacy_length);
    *response_length = (uint32_t)legacy_length;
    return true;
}

void app_main(void){
    bool setup_fail = false;
    /* No se conserva estado de tareas entre reinicios del ESP32. */
    task_store_init(&deployed_tasks);
    // << Setup ESP32 serial interface >>
    esp32_uart_t uart;
    esp32_uart_configs_t uart_conf = {
        .port_num = UART_PORT,
        .baudrate = SERIAL_BAUDRATE,
        .rx_buffer_size = 1024,
        .tx_buffer_size = 0,
        .tick_ms = 100
    };

    serial_errno_t conf_errno = serial_configure(
        &uart, &uart_conf
    );
    if (!setup_fail) setup_fail = (conf_errno != SERIAL_OK);

    // << Setup the protocol interface >>
    // [TX:output]
    protocol_frame_t tx_frame;
    protocol_tx_t tx;
    serial_packet_t tx_packet;
    protocol_errno_t tx_setup_errno = protocol_tx_init(&tx, &tx_packet);

    // [RX:input]
    protocol_frame_t rx_frame;
    protocol_rx_t rx;
    serial_packet_t rx_packet;
    protocol_errno_t rx_setup_errno = protocol_rx_init(&rx, &rx_packet);

    if (!setup_fail) setup_fail = (tx_setup_errno != PROTOCOL_OK || rx_setup_errno != PROTOCOL_OK);
    // << Setup GPIO >>
    board_gpio_init(LED0, GPIO_OUTPUT);
    board_gpio_init(LED1, GPIO_OUTPUT);
    board_gpio_init(LED2, GPIO_OUTPUT);
    board_gpio_init(LED3, GPIO_OUTPUT);
    
    board_gpio_write(LED0, GPIO_LOW);
    board_gpio_write(LED1, GPIO_LOW);
    board_gpio_write(LED2, GPIO_LOW);
    board_gpio_write(LED3, GPIO_LOW);
    // << Setup basic messages >>
    char response[MESSAGE_LEN];
    // << Procedure loop >>
    int state = 0;

    if (setup_fail) board_gpio_write(LED0, GPIO_HIGH);
    else board_gpio_write(LED1, GPIO_HIGH);
    while (1) {
        if (setup_fail) continue;
        // [RX state]
        if (state == 0){
            board_gpio_write(LED2, GPIO_HIGH);
            board_gpio_write(LED3, GPIO_LOW);
            // [Poll RX state machine]
            protocol_errno_t rx_errno = protocol_rx_poll(
                &uart,
                &rx,
                &rx_packet
            );
            // [Check RX]
            if (rx.complete){
                uint32_t response_length = 0u;
                if (rx.valid){
                    protocol_errno_t rx_frame_errno = protocol_set_buffer_to_frame(
                        &rx_frame,
                        rx.buffer,
                        sizeof(rx.buffer)
                    );
                    if (rx_frame_errno == PROTOCOL_OK)
                        (void)prepare_response(&rx_frame, response,
                            sizeof(response), &response_length);
                }
                if (response_length == 0u) {
                    /* Mantiene una respuesta legible para mensajes no soportados. */
                    const char *fallback = "ESP32 didn't understand message";
                    response_length = (uint32_t)strlen(fallback) + 1u;
                    memcpy(response, fallback, response_length);
                }

                tx_frame.relevance = 0;
                tx_frame.length = response_length;
                memcpy(tx_frame.payload, response, response_length);

                protocol_errno_t tx_frame_errno = protocol_set_frame_to_buffer(
                    &tx_frame,
                    tx.buffer,
                    sizeof(tx.buffer)
                );
                // [Move to TX]
                state = 1;
            }
        }
        // [TX state]
        if (state == 1){
            board_gpio_write(LED2, GPIO_LOW);
            board_gpio_write(LED3, GPIO_HIGH);
            // [Poll TX state machine]
            protocol_errno_t tx_errno = protocol_tx_poll(
                &uart,
                &tx,
                &tx_packet
            );
            // [Check TX]
            if (tx.complete && tx.valid){
                // [Move to RX]
                state = 0;
            }
        }
    }
}