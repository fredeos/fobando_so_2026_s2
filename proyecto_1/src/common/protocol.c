#include "common/protocol.h"

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
)
{
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