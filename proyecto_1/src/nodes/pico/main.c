

#include <string.h>
#include <stdio.h>

#include "pico/stdlib.h"

#include "serial/pico.h"
#include "serial/serial.h"
#include "common/protocol.h"
#include "common/message_parser.h"
#include "common/messages.h"
#include "common/task_store.h"
#include "calibrator.h"
#include "scheduler_runtime.h"
#include "gpio/gpio.h"

#define LED0 22
#define LED1 26
#define LED2 27
#define LED3 28

#define NODE_ID 2u
#define MESSAGE_LEN (PROTOCOL_PAYLOAD_BYTES + 1u)

// Estado de configuración y ejecución del nodo.
static uint32_t configured_tick_ms;
static MessageAlgorithm configured_algorithm = MESSAGE_SCHED_RMS;
static uint32_t configured_ut_per_tick;
static uint64_t next_tick_us;

// Las tareas desplegadas y el scheduler permanecen en RAM mientras el nodo está encendido.
static TaskStore deployed_tasks;
static SchedulerRuntime scheduler_runtime;

/**
 * @brief Crea y serializa una respuesta a un mensaje recibido.
 *
 * Conserva el identificador de la petición original y establece el identificador
 * del nodo local. Según el tipo de respuesta, incluye la operación confirmada,
 * la razón del rechazo o el resultado de calibración.
 *
 * @param request           Mensaje recibido al que se debe responder.
 * @param type              Tipo de respuesta que se va a generar.
 * @param reason            Motivo del rechazo, si la respuesta es MSG_NACK.
 * @param ut_per_tick       Unidades de trabajo por tick, si corresponde.
 * @param response          Búfer donde se almacenará el mensaje serializado.
 * @param response_capacity Capacidad del búfer de respuesta.
 * @param response_length   Dirección donde se guardará la longitud del mensaje.
 *
 * @return true si la serialización fue exitosa; false en caso contrario.
 */
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
    } else if (type == MSG_NACK) {
        reply.data.reply.type = request->type;
        reply.data.reply.reason = reason;
    } else {
        reply.data.ut_per_tick = ut_per_tick;
    }

    size_t json_length = 0u;
    if (message_serialize(&reply, response, response_capacity,
                          &json_length) != MESSAGE_SERIALIZE_OK) {
        return false;
    }

    *response_length = (uint32_t)json_length;
    return true;
}


/**
 * @brief Calibra el nodo y calcula el promedio de unidades de trabajo por tick.
 *
 * Ejecuta varias muestras de calibración y calcula el promedio de sus
 * resultados, reservando un 20 % del tiempo disponible.
 *
 * @param sample_ticks Cantidad de muestras que se realizarán.
 * @param ut_per_tick  Dirección donde se almacenará el promedio calculado.
 *
 * @return true si la calibración termina correctamente; false si los parámetros
 *         son inválidos, alguna muestra falla o el resultado está fuera de rango.
 */
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


/**
 * @brief Procesa un mensaje recibido y prepara la respuesta correspondiente.
 *
 * Interpreta los mensajes del protocolo para configurar el nodo, calibrarlo,
 * desplegar tareas, iniciar o detener el scheduler y activar tareas esporádicas.
 * Genera respuestas ACK o NACK según el resultado de cada operación.
 * Si el mensaje no pertenece al protocolo actual, conserva la compatibilidad
 * con los comandos de prueba PING y MARCO.
 *
 * @param received          Trama recibida por el nodo.
 * @param response          Buffer donde se almacenará la respuesta.
 * @param response_capacity Capacidad del buffer de respuesta.
 * @param response_length   Dirección donde se guardará la longitud de la respuesta.
 *
 * @return true si la respuesta se prepara correctamente; false si el mensaje
 *         es inválido, no corresponde al nodo o no hay espacio suficiente.
 */
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
            if (scheduler_runtime.prepared) {
                return serialize_reply(&incoming, MSG_NACK,
                    REJECT_INVALID_STATE, 0u, response, response_capacity,
                    response_length);
            }
            configured_tick_ms = incoming.data.config.tick_ms;
            configured_algorithm = incoming.data.config.algorithm;
            return serialize_reply(&incoming, MSG_ACK, REJECT_NONE, 0u,
                response, response_capacity, response_length);
        }

        if (incoming.type == MSG_CALIBRATE_REQUEST) {
            if (scheduler_runtime.prepared) {
                return serialize_reply(&incoming, MSG_NACK,
                    REJECT_INVALID_STATE, 0u, response, response_capacity,
                    response_length);
            }
            uint32_t ut_per_tick = 0u;
            if (calibrate(incoming.data.sample_ticks, &ut_per_tick)) {
                configured_ut_per_tick = ut_per_tick;
                return serialize_reply(&incoming, MSG_CALIBRATE_RESPONSE,
                    REJECT_NONE, ut_per_tick, response, response_capacity,
                    response_length);
            }
            return serialize_reply(&incoming, MSG_NACK,
                REJECT_CALIBRATION_FAILED, 0u, response, response_capacity,
                response_length);
        }

        if (incoming.type == MSG_DEPLOY_TASK) {
            if (scheduler_runtime.prepared) {
                return serialize_reply(&incoming, MSG_NACK,
                    REJECT_INVALID_STATE, 0u, response, response_capacity,
                    response_length);
            }
            /*
             * Guarda Task y cost_ticks ya calculado por el Host. El ACK confirma
             * almacenamiento, no admisión local ni inicio del scheduler.
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

        if (incoming.type == MSG_START) {
            bool was_running = scheduler_runtime.running;
            RejectReason reason = REJECT_INVALID_STATE;
            SchedulerType scheduler_type =
                configured_algorithm == MESSAGE_SCHED_EDF ? EDF : RMS;
            if (!scheduler_runtime_start(&scheduler_runtime, &deployed_tasks,
                                         configured_ut_per_tick,
                                         scheduler_type, &reason)) {
                return serialize_reply(&incoming, MSG_NACK, reason, 0u,
                    response, response_capacity, response_length);
            }
            if (!was_running)
                next_tick_us = time_us_64() +
                    (uint64_t)configured_tick_ms * 1000u;
            return serialize_reply(&incoming, MSG_ACK, REJECT_NONE, 0u,
                response, response_capacity, response_length);
        }

        if (incoming.type == MSG_STOP) {
            scheduler_runtime_stop(&scheduler_runtime);
            return serialize_reply(&incoming, MSG_ACK, REJECT_NONE, 0u,
                response, response_capacity, response_length);
        }

        if (incoming.type == MSG_TRIGGER_SPORADIC) {
            RejectReason reason = REJECT_INVALID_STATE;
            if (!scheduler_runtime_trigger_sporadic(
                    &scheduler_runtime, incoming.data.task_id, &reason)) {
                return serialize_reply(&incoming, MSG_NACK, reason, 0u,
                    response, response_capacity, response_length);
            }
            return serialize_reply(&incoming, MSG_ACK, REJECT_NONE, 0u,
                response, response_capacity, response_length);
        }

        return false;
    }

    const char *legacy = "Pico didn't understand message";
    if (request_length == 4u && memcmp(request, "PING", 4u) == 0)
        legacy = "PONG from Pico";
    else if (request_length == 5u && memcmp(request, "MARCO", 5u) == 0 &&
             received->relevance > 0u)
        legacy = "POLO from Pico";

    size_t legacy_length = strlen(legacy) + 1u;
    if (legacy_length > response_capacity)
        return false;
    memcpy(response, legacy, legacy_length);
    *response_length = (uint32_t)legacy_length;
    return true;
}


/**
 * @brief Ejecuta un tick del scheduler cuando corresponde.
 *
 * Comprueba si el scheduler está activo y si se alcanzó el momento del
 * siguiente tick. Si la ejecución falla, enciende LED0 y detiene el
 * scheduler. Después de cada tick, programa el siguiente usando el tiempo
 * actual para evitar ejecutar ticks atrasados en ráfaga.
 */
static void run_scheduler_if_due(void)
{
    if (!scheduler_runtime.running || configured_tick_ms == 0u)
        return;

    if (time_us_64() < next_tick_us)
        return;

    uint32_t selected_task_id;
    if (!scheduler_runtime_run_tick(&scheduler_runtime, &selected_task_id)) {
        board_gpio_write(LED0, GPIO_HIGH);
        scheduler_runtime_stop(&scheduler_runtime);
        return;
    }

    /* Evita ejecutar ticks atrasados en ráfaga y deja tiempo al polling serial. */
    next_tick_us = time_us_64() + (uint64_t)configured_tick_ms * 1000u;
}

/**
 * @brief Inicializa el nodo y ejecuta su ciclo principal.
 *
 * Configura el almacenamiento de tareas, el scheduler, la comunicación serial,
 * el protocolo y los GPIO. Después, procesa continuamente las tramas recibidas,
 * genera y transmite respuestas, y ejecuta los ticks del scheduler cuando
 * corresponde.
 *
 * Si falla la configuración inicial, el ciclo principal no procesa mensajes
 * ni ejecuta el scheduler.
 */
void main(void){
    bool setup_fail = false;
    /* Cada arranque comienza con una tabla de tareas vacía. */
    task_store_init(&deployed_tasks);
    scheduler_runtime_init(&scheduler_runtime);

    // << Setup Pico serial interface >>
    pico_serial_t serial;
    pico_serial_configs_t serial_conf = {
        .timeout_us = 0u
    };

    serial_errno_t conf_errno = serial_configure(
        &serial,
        &serial_conf
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

    if (setup_fail) board_gpio_write(LED0, GPIO_LOW);
    else board_gpio_write(LED1, GPIO_LOW);
    while (1) {
        if (setup_fail) continue;
        // [RX state]
        if (state == 0){
            board_gpio_write(LED2, GPIO_HIGH);
            board_gpio_write(LED3, GPIO_LOW);
            // [Poll RX state machine]
            protocol_errno_t rx_errno = protocol_rx_poll(
                &serial,
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
                    const char *fallback = "Pico didn't understand message";
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
                &serial,
                &tx,
                &tx_packet
            );
            // [Check TX]
            if (tx.complete && tx.valid){
                // [Move to RX]
                state = 0;
            }
        }
        run_scheduler_if_due();
    }
}