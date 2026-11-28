#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#include "common/json.h"

#include "client/protocol/worker.h"
#include "common/protocol.h"
#include "serial/serial.h"
#include "serial/posix.h"

#include "client/timing.h"

#define MESSAGE_LENGTH 128u

#define LIFETIME_MS 15000u  // 15000 ms (15s)
#define ACTION_MS   200u    // 200 ms (0.2s)

#define TIMEOUT_MS  1000u   // 1000 ms (1s)
#define ACCEPT_CRITERIA 1u  // prioridad 1

#define PARSE(operation) do { \
    JsonParseResult result_ = (operation); \
    if (result_ != JSON_PARSE_OK) { \
        printf("json_parse(): %s\n", parse_error(result_)); \
    } \
} while (0)

#define SERIALIZE(operation) do { \
    JsonSerializeResult result_ = (operation); \
    if (result_ != JSON_SERIALIZE_OK) {\
        printf("json_parse(): %s\n", serialize_error(result_)); \
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
    // + Interfaz de protocolo CRC-16
    protocol_worker_t worker;
    SETUP ( init_pworker(0, argv[1], true, TIMEOUT_MS, ACCEPT_CRITERIA, &worker) != 0); // configurar propiedades
    SETUP ( start_pworker(&worker) != 0 ); // inicializar hilo de comunicacion
    // -------------------------------------------------
    // Variables del programa
    // -------------------------------------------------
    // + Buffers de mensajes
    char input[MESSAGE_LENGTH]  = { 0 };
    char output[MESSAGE_LENGTH] = { 0 };
    // + Campos JSON recibidos
    char event [24] = { 0 };
    char action[24] = { 0 };
    // + Campos JSON para enviar
    int value = 0; // accion
    // -------------------------------------------------
    // Loop de protocolo
    // -------------------------------------------------
    uint64_t first = get_time_ms();
    uint64_t last = first;
    while (1) {
        /* Obtener tiempo actual */
        uint64_t now = get_time_ms();
        if (now - first >= LIFETIME_MS) {
            kill_pworker(&worker);
            break;
        }
        /* Recibir mensajes  */
        if (worker.in.available){
            // Cargar mensaje
            if (pqueue_consume(input, sizeof(input), NULL, &worker.in) == 1){
                // Definir campos esperados
                JsonField fields[] = {
                    {.name = "event", .pair = { 0 }},
                    {.name = "action", .pair = { 0 }}
                };
                // Parsear objeto
                if (read_object(input, strlen(input), fields, COUNT(fields)) == JSON_PARSE_OK){
                    // Extraer valores
                    PARSE( read_string(&fields[0], event, sizeof(event)) );
                    PARSE( read_string(&fields[1], action, sizeof(action)) );
                    // Mostrar mensaje leido
                    printf("Received: <event:%s> <action:%s> \n", event, action);
                    // Limpiar buffers
                    memset(event, 0, sizeof(event));
                    memset(action, 0, sizeof(action));
                }
                // Limpiar buffer
                memset(input, 0, sizeof(input));
            }
        }
        /* Escribir mensajes */
        if (now - last >= ACTION_MS){
            // Definir campos del mensaje
            char command[] = "led";
            JsonString command_str = {.string = command, .length = strlen(command)};

            JsonPair pairs[] = {
                {.name = "command", .value = &command_str, .size = sizeof(command_str), .type = JSON_STRING},
                {.name = "action", .value = &value, .size = sizeof(value), .type = JSON_INTEGER}
            };
            JsonObject object = {.pairs = pairs, .count = COUNT(pairs)};
            // Serializar mensaje
            size_t written = 0;
            if (write_object(&object, output, sizeof(output), &written, true) == JSON_SERIALIZE_OK){
                output[written] = '\0';
                printf("Sent: %s \n", output);
                // Cargar mensaje a la cola
                while (pqueue_append(output, strlen(output)+1, ACCEPT_CRITERIA+1, &worker.out) > 0);
            }
            // Limpiar buffer
            memset(output, 0, sizeof(output));
            // Sumar valor
            value++;
            if (value == 3) value = 0;
            // Actualizar reloj
            last = now;
        }
    }
    // -------------------------------------------------
    // Final del programa
    // -------------------------------------------------
    while (worker.running);
    printf("Final del progarama\n");
    return 0;
}