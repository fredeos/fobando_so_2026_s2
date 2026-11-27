#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#include "common/json.h"

#include "common/protocol.h"
#include "serial/serial.h"
#include "serial/posix.h"

#include "client/timing.h"

#define MESSAGE_LENGTH 128u
#define LIFETIME_MS 15000u  // 15000 ms (15s)
#define ACTION_MS   500u    // 500 ms (0.5s)
#define ACCEPT_CRITERIA 1u

#define PARSE(operation) do { \
    JsonParseResult result_ = (operation); \
    if (result_ != JSON_PARSE_OK) { \
        printf("json_parse(): %s\n", parse_error(result_)); \
        break; \
    } \
} while (0)

#define SETUP(operation) do { \
    if (operation){ \
        printf("setup_failed()\n"); \
        return 1; \
    } \
} while (0)

int main(int argc, char *argv[]){
    if (argc < 2) {
        printf("No device provided: /dev/tty**** \n");
        return 1;
    }
    // -------------------------------------------------
    // Configuracion de interfaces
    // -------------------------------------------------
    // + Interfaz serial
    int fd = -1; // file descriptor
    posix_serial_config_t config = {
        .device = argv[1],
        .baudrate = SERIAL_BAUDRATE
    };
    SETUP( serial_configure(&fd, &config) != SERIAL_OK);
    // + Interfaz de protocolo CRC-16
    // [TX:output]
    protocol_frame_t tx_frame;
    protocol_tx_t tx;
    serial_packet_t tx_packet;
    SETUP ( protocol_tx_init(&tx, &tx_packet) != PROTOCOL_OK );
    // [RX:input]
    protocol_frame_t rx_frame;
    protocol_rx_t rx;
    serial_packet_t rx_packet;
    SETUP ( protocol_rx_init(&rx, &rx_packet) != PROTOCOL_OK );
    // -------------------------------------------------
    // Variables del programa
    // -------------------------------------------------
    // + Mensajes genericos
    const char* ping = "PING";
    // + Buffers de comunicacion
    char input[MESSAGE_LENGTH] = { 0 };
    char output[MESSAGE_LENGTH] = { 0 };
    // + Campos json recibidos
    char event [24] = { 0 };
    char action[24] = { 0 };
    // -------------------------------------------------
    // Loop de protocolo
    // -------------------------------------------------
    int state = 0; // estado de FSM
    int value = 0; // accion
    uint64_t last  = get_time_ms();
    uint64_t first = last;
    while (1) {
        /* (0) Configurar mensaje */
        /* En esta seccion se configura el mensaje por enviar */
        if (state == 0){
            char* message = ping;
            tx_frame.relevance = 0u;
            // Obtener estamp de tiempo y validar momento del programa
            uint64_t now = get_time_ms();
            if (now - first >= LIFETIME_MS){ // fin del programa
                break;
            } else if (now - last >= ACTION_MS){ // enviar comando al ESP32
                // Definir prioridad
                tx_frame.relevance = ACCEPT_CRITERIA+1;
                // Crear objeto
                const char* command = "led";
                JsonString command_str = {.string = command, .length = strlen(command)};

                JsonPair pairs[] = {
                    {.name = "command", .value = &command_str, .size = sizeof(command_str), .type = JSON_STRING},
                    {.name = "action", .value = &value, .size = sizeof(value), .type = JSON_INTEGER}
                };
                JsonObject object = {.pairs = pairs, .count = COUNT(pairs)};
                // Serializar objeto
                size_t written = 0;
                write_object(&object, output, sizeof(output), &written, true);
                output[written] = '\0';
                // Copiar objeto completo
                memcpy(tx_frame.payload, output, strlen(output)+1);
                tx_frame.length = strlen(output) + 1;
                // Cambiar accion
                value++;
                if (value == 2) value = 0;
                // Actulizar valor de temporizador
                last = now;
                // Limpiar buffer
                memset(output, 0, sizeof(output));
            } else { // mensaje default
                memcpy(tx_frame.payload, message, strlen(message)+1);
                tx_frame.length = strlen(message) + 1;
            }
            protocol_set_frame_to_buffer(&tx_frame, tx.buffer, sizeof(tx.buffer));
            // Mover a TX
            state = 1;
        }
        /* (1) TX state */
        /* TX se encarga de enviar mensajes */
        if (state == 1){
            // << Poll TX FSM >>
            protocol_tx_poll(&fd, &tx, &tx_packet);
            // << Check TX status >>
            if (tx.complete && tx.valid){
                memset(tx.buffer, 0, sizeof(tx.buffer));
                // Mover a RX
                state = 2;
            }
        }
        /* (2) RX state */
        /* RX se encarga de recibir mensajes */
        if (state == 2){
            // << Poll RX FSM >>
            protocol_rx_poll(&fd, &rx, &rx_packet);
            // << Check RX status >>
            if (rx.complete && rx.valid){
                protocol_set_buffer_to_frame(&rx_frame, rx.buffer, sizeof(rx.buffer));
                // Capturar mensajes de prioridad
                if (rx_frame.relevance >= ACCEPT_CRITERIA){
                    // Extraer mensaje
                    memcpy(input, rx_frame.payload, rx_frame.length);
                    // Definir campos esperados
                    JsonField fields[] = {
                        {.name = "event", .pair = { 0 }},
                        {.name = "action", .pair = { 0 }}
                    };
                    // Parseo del objeto
                    if (read_object(input, strlen(input), fields, COUNT(fields)) == JSON_PARSE_OK){
                        // Leer valores
                        read_string(&fields[0], event, sizeof(event));
                        read_string(&fields[1], action, sizeof(action));
                        // Mostrar valores leidos
                        printf("ESP32: <event:%s> <action:%s> \n", event, action);
                        // Limpiar valores leidos
                        memset(event, 0, sizeof(event));
                        memset(action, 0, sizeof(action));
                    }
                    memset(input, 0, sizeof(input)); // limpiar buffer
                }
                // Mover a configuracion
                state = 0;
                memset(rx.buffer, 0, sizeof(rx.buffer));
            }
        }
    }
    // -------------------------------------------------
    // Final del programa
    // -------------------------------------------------
    close(fd);
    return 0;
}