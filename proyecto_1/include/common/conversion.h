#ifndef COMMON_CONVERSION_H
#define COMMON_CONVERSION_H

#include <stdint.h>

/* 
 * This header file contains functions for big endian byte conversions of data types.
 * This helps avoid issues with endianness on differente systems
 *
*/

// --------------------------------------------------------------------

void write_u16_be(
    uint8_t* dst,
    uint16_t value
);
void write_u32_be(
    uint8_t* dst,
    uint32_t value
);

uint16_t read_u16_be(
    const uint8_t* src
);

uint32_t read_u32_be(
    const uint8_t* src
);

#endif // COMMON_CONVERSION_H