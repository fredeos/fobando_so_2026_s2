#ifndef COMMON_PROTOCOL_H
#define COMMON_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

#include "serial/serial.h"
#include "common/conversion.h"

/* Identificadores del inicio de trama */
#define PROTOCOL_MAGIC_0 0xA5u
#define PROTOCOL_MAGIC_1 0x5Au

/* Versión del protocolo */
#define PROTOCOL_VERSION 1u

/* Tamaños de los componentes de una trama */
#define PROTOCOL_HEADER_BYTES    2u
#define PROTOCOL_VERSION_BYTES   1u
#define PROTOCOL_RELEVANCE_BYTES 1u
#define PROTOCOL_LENGTH_BYTES    4u
#define PROTOCOL_PAYLOAD_BYTES   256u
#define PROTOCOL_CRC_BYTES       2u

/* Offsets de los componentes de la trama */
#define PROTOCOL_HEADER_OFFSET    0u
#define PROTOCOL_VERSION_OFFSET   PROTOCOL_HEADER_BYTES
#define PROTOCOL_RELEVANCE_OFFSET \
    (PROTOCOL_VERSION_OFFSET + PROTOCOL_VERSION_BYTES)
#define PROTOCOL_LENGTH_OFFSET \
    (PROTOCOL_RELEVANCE_OFFSET + PROTOCOL_RELEVANCE_BYTES)
#define PROTOCOL_PAYLOAD_OFFSET  \
    (PROTOCOL_LENGTH_OFFSET + PROTOCOL_LENGTH_BYTES)
#define PROTOCOL_CRC_OFFSET \
    (PROTOCOL_PAYLOAD_OFFSET + PROTOCOL_PAYLOAD_BYTES)

/* Tamaños de una trama */
#define PROTOCOL_FRAME_BYTES \
    (PROTOCOL_CRC_OFFSET + PROTOCOL_CRC_BYTES)

/* NOTA: para el protocolo CRC-16 se valida sobre los bytes de version hasta el payload */

// ---------------------------------------------------------------------------------------
/*
 * Tipos de errores para operaciones del protocolo
 *
*/
typedef enum {
    PROTOCOL_OK = 0,
    // errores generales
    PROTOCOL_ERROR = -1,
    PROTOCOL_OUT_RANGE    = -2,
    PROTOCOL_INVALID_ARG  = -3,
    PROTOCOL_INVALID_SIZE = -4,
    PROTOCOL_INVALID_MODE = -5
} protocol_errno_t;

typedef enum {
    PROTOCOL_ACK  = 0,
    PROTOCOL_NACK,
    PROTOCOL_ACK_UNKNOWN,
    PROTOCOL_ACK_TIMEOUT
} protocol_ack_t;


#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------------------
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

#endif // COMMON_PROTOCOL_H