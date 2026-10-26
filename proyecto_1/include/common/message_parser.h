#ifndef COMMON_MESSAGE_PARSER_H
#define COMMON_MESSAGE_PARSER_H

#include <stddef.h>
#include <stdint.h>

#include "common/events.h"
#include "common/messages.h"

/* Límite del contenido JSON, independiente de las cabeceras de transporte */
#define MESSAGE_MAX_BYTES 256u

/* Algoritmo local solicitado al nodo, sin dependencias del Host */
typedef enum {
    MESSAGE_SCHED_RMS = 1,
    MESSAGE_SCHED_EDF
} MessageAlgorithm;

/* Resultado de interpretar el contenido de un mensaje */
typedef enum {
    MESSAGE_PARSE_OK = 0,
    MESSAGE_PARSE_INVALID_ARGUMENT,
    MESSAGE_PARSE_TOO_LARGE,
    MESSAGE_PARSE_INVALID_JSON,
    MESSAGE_PARSE_INVALID_SCHEMA,
    MESSAGE_PARSE_OUT_OF_RANGE,
    MESSAGE_PARSE_UNKNOWN_TYPE
} MessageParseResult;

typedef enum {
    MESSAGE_SERIALIZE_OK = 0,
    MESSAGE_SERIALIZE_INVALID_ARGUMENT,
    MESSAGE_SERIALIZE_INVALID_MESSAGE,
    MESSAGE_SERIALIZE_TOO_LARGE
} MessageSerializeResult;

/*
 * Mensaje interpretado, compartido por Host, ESP32 y Pico.
 *
 * type determina el miembro válido de data.
 * request_id permite asociar ACK/NACK con una solicitud.
 * No contiene punteros al string recibido ni recursos que liberar.
 */
typedef struct {
    MessageType type;
    uint32_t node_id;
    uint32_t request_id;

    union {
        struct {
            MessageAlgorithm algorithm;
            uint32_t tick_ms;
            uint32_t report_every_ticks;
        } config;

        uint32_t sample_ticks;  // CALIBRATE_REQUEST
        uint32_t ut_per_tick;   // CALIBRATE_RESPONSE

        struct {
            Task task;
            uint32_t cost_ticks;
        } deploy_task;

        Event event;           // EVENT, origen NODE y hora del Host en cero

        struct {
            uint64_t tick;
            uint32_t task_id;  // Identificador informado por el nodo
        } tick_report;

        uint32_t task_id;       // TRIGGER_SPORADIC

        struct {
            MessageType type;  // Tipo de la solicitud respondida
            RejectReason reason;
        } reply;               // ACK o NACK
    } data;
} Message;

/*
 * Interpreta un mensaje JSON completo según docs/messages.md.
 *
 * text: bytes de entrada; no requiere terminador NUL y no se modifica.
 * length: longitud exacta del JSON, sin NUL ni relleno de transporte.
 * out: recibe el mensaje únicamente si la operación tiene éxito.
 *
 * Retorna MESSAGE_PARSE_OK o el motivo del rechazo.
 * No lee fuera de length, no usa heap y no realiza operaciones seriales.
 * La entrada y out deben ocupar regiones de memoria independientes.
 */
MessageParseResult message_parse(
    const char *text,
    size_t length,
    Message *out
);

/*
 * Serializa un mensaje usando el mismo esquema que message_parse().
 * capacity incluye espacio para el terminador NUL; *length recibe la
 * longitud JSON sin NUL. El buffer y length solo se modifican al tener éxito.
 */
MessageSerializeResult message_serialize(
    const Message *message,
    char *buffer,
    size_t capacity,
    size_t *length
);

/* Devuelve una descripción constante del resultado, sin reservar memoria */
const char *message_parse_result_string(MessageParseResult result);

#endif // COMMON_MESSAGE_PARSER_H
