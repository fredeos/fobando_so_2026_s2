// Implementation file of conversion.h
#include "common/conversion.h"

void write_u16_be(
    uint8_t* dst,
    uint16_t value
){
    dst[0] = (uint8_t)(value >> 8);
    dst[1] = (uint8_t)(value);
}

void write_u32_be(
    uint8_t* dst,
    uint32_t value
){
    dst[0] = (uint8_t)(value >> 24);
    dst[1] = (uint8_t)(value >> 16);
    dst[2] = (uint8_t)(value >> 8);
    dst[3] = (uint8_t)(value);
}

uint16_t read_u16_be(
    const uint8_t* src
){
    return ((uint16_t)src[0] << 8) |
           ((uint16_t)src[1]);
}

uint32_t read_u32_be(
    const uint8_t* src
){
    return ((uint32_t)src[0] << 24) |
           ((uint32_t)src[1] << 16) |
           ((uint32_t)src[2] << 8)  |
           ((uint32_t)src[3]);
}