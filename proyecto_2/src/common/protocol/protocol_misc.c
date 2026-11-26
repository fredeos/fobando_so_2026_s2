// > Archivo de implementacion <protocol.h>
#include "common/protocol.h"

#include <string.h>

#include "common/conversion.h"
// =================================================================================================

/*
 * Implementación del CRC-16/CCITT-FALSE.
 *
 * Polinomio: 0x1021
 * Valor inicial: 0xFFFF
 * XOR final: 0x0000
 * Sin reflexión de bits.
 */
uint16_t protocol_crc16(
    const uint8_t *data,
    size_t length
){
    uint16_t crc = 0xFFFFu;

    // Procesar cada byte
    for (size_t i = 0; i < length; ++i) {

        crc ^= (uint16_t)(data[i] << 8);

        // Procesar los 8 bits del byte
        for (unsigned bit = 0; bit < 8; ++bit) {

            if ((crc & 0x8000u) != 0u) {

                crc = (uint16_t)(
                    (crc << 1) ^ 0x1021u
                );

            } else {

                crc = (uint16_t)(crc << 1);

            }
        }
    }

    return crc;
}

// =================================================================================================

protocol_errno_t protocol_set_frame_to_buffer(
    protocol_frame_t* src,
    uint8_t* dst,
    size_t length
){
    // << Identificar excepcions >>
    if (src == NULL || dst == NULL)
        return PROTOCOL_INVALID_ARG;
    if (length != PROTOCOL_FRAME_BYTES)
        return PROTOCOL_INVALID_SIZE;
    if (src->length > PROTOCOL_PAYLOAD_BYTES)
        return PROTOCOL_OUT_RANGE;
    // << Cargar componentes >>
    // 1. Cargar encabezado
    for (size_t i = 0; i < PROTOCOL_HEADER_BYTES; i++){
        size_t mg = i % 2;
        if (mg == 0) dst[i] = PROTOCOL_MAGIC_0;
        if (mg == 1) dst[i] = PROTOCOL_MAGIC_1;
    }
    memcpy(
        src->header,
        dst,
        PROTOCOL_HEADER_BYTES
    );
    // 2. Cargar version
    dst[PROTOCOL_VERSION_OFFSET] = PROTOCOL_VERSION;
    // 3. Cargar relevancia
    dst[PROTOCOL_RELEVANCE_OFFSET] = src->relevance;
    // 4. Cargar largo
    write_u32_be(
        dst + PROTOCOL_LENGTH_OFFSET,
        src->length
    );
    // 5. Cargar payload
    memcpy(
        dst + PROTOCOL_PAYLOAD_OFFSET,
        src->payload,
        PROTOCOL_PAYLOAD_BYTES
    );
    // 6. Generar CRC-16
    uint16_t crc = protocol_crc16(
        dst + PROTOCOL_VERSION_OFFSET,
        PROTOCOL_CRC_OFFSET - PROTOCOL_VERSION_OFFSET
    );
    src->crc = crc;
    write_u16_be(
        dst + PROTOCOL_CRC_OFFSET,
        crc
    );
    // << Retornar >>
    return PROTOCOL_OK;
}

// =================================================================================================

protocol_errno_t protocol_set_buffer_to_frame(
    protocol_frame_t* dst,
    const uint8_t* src,
    size_t length
){
    // << Identificar excepcions >>
    if (src == NULL || dst == NULL)
        return PROTOCOL_INVALID_ARG;
    if (length != PROTOCOL_FRAME_BYTES)
        return PROTOCOL_INVALID_SIZE;
    // << Cargar componentes >>
    // 1. Cargar encabezado
    memcpy(
        dst->header,
        src,
        PROTOCOL_HEADER_BYTES
    );
    // 2. Cargar version
    dst->version = src[PROTOCOL_VERSION_OFFSET];
    // 3. Cargar relevancia
    dst->relevance = src[PROTOCOL_RELEVANCE_OFFSET];
    // 4. Cargar largo
    dst->length = read_u32_be(
        src + PROTOCOL_LENGTH_OFFSET
    );
    // 5. Cargar payload
    memcpy(
        dst->payload,
        src + PROTOCOL_PAYLOAD_OFFSET,
        PROTOCOL_PAYLOAD_BYTES
    );
    // 6. Cargar el CRC-16
    dst->crc = read_u16_be(
        src + PROTOCOL_CRC_OFFSET
    );
    // << Retornar >>
    return PROTOCOL_OK;
}

// =================================================================================================