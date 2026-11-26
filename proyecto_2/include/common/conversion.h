#ifndef COMMON_CONVERSION_H
#define COMMON_CONVERSION_H
/* 
 * <conversion.h>: este archivo empaqueta funciones para abstraer el endianess de
 * la arquitectura; que es necesario para implementar protocolos de comunicacion
 * entre dispositivos con diferentes arquitecturas. Este incluye funciones para
 * leer y escribir datos sin signo (raw o crudos) de 16 y 32 bits usando big endian
 * (independiente de la arquitectura los bytes quedan ordenados en big endian)
 * 
 * Implementacion: 'src/common/conversion.c'
*/
// ===========================================================================
// Includes
// ===========================================================================
// + C libraries
#include <stdint.h>

// ===========================================================================
// Definiciones
// ===========================================================================


// ===========================================================================
// Declaraciones y tipos
// ===========================================================================
#ifdef __cplusplus
extern "C" {
#endif

/* Escribir 16 bits (2 bytes) en big endian */
void write_u16_be(
    uint8_t* dst,
    uint16_t value
);

/* Escribir 32 bits (4 bytes) en big endian */
void write_u32_be(
    uint8_t* dst,
    uint32_t value
);

/* Leer 16 bits (2 bytes) en big endian */
uint16_t read_u16_be(
    const uint8_t* src
);

/*Leer 32 bits (4 bytes) en big endian */
uint32_t read_u32_be(
    const uint8_t* src
);

#ifdef __cplusplus
}
#endif
// ===========================================================================
#endif // COMMON_CONVERSION_H