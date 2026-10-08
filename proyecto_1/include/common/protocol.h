#ifndef COMMON_PROTOCOL_H
#define COMMON_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

/* Identificadores del inicio de trama */
#define PROTOCOL_MAGIC_0 0xA5u
#define PROTOCOL_MAGIC_1 0x5Au

/* Versión del protocolo */
#define PROTOCOL_VERSION 1u

/* Tamaños de los componentes de una trama */
#define PROTOCOL_HEADER_BYTES 8u
#define PROTOCOL_CRC_BYTES    2u
#define PROTOCOL_MAX_PAYLOAD  256u

#define PROTOCOL_MAX_FRAME \
    (PROTOCOL_HEADER_BYTES + \
     PROTOCOL_MAX_PAYLOAD + \
     PROTOCOL_CRC_BYTES)

/*
 * Tipos de mensajes intercambiados
 * entre el Host y los microcontroladores.
 */
typedef enum {
    MSG_CONFIG = 1,
    MSG_CALIBRATE_REQUEST,
    MSG_CALIBRATE_RESPONSE,
    MSG_DEPLOY_TASK,
    MSG_START,
    MSG_EVENT,
    MSG_TICK_REPORT,
    MSG_TRIGGER_SPORADIC,
    MSG_STOP,
    MSG_ACK,
    MSG_NACK
} MessageType;

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Calcula el CRC-16/CCITT-FALSE.
 *
 * data: bytes sobre los que se calcula el CRC.
 * length: cantidad de bytes.
 *
 * Retorna el CRC de 16 bits.
 */
uint16_t protocol_crc16(
    const uint8_t *data,
    size_t length
);

#ifdef __cplusplus
}
#endif

#endif