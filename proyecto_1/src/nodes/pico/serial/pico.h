#ifndef SERIAL_PICO_H
#define SERIAL_PICO_H

#include <stdint.h>

/*
 * Structure for Pico serial interface using USB CDC
 *
 */
typedef struct {
    uint32_t timeout_us;
} pico_serial_t;


/*
 * Structure for Pico serial interface configuration
 *
 */
typedef struct {
    uint32_t timeout_us;
} pico_serial_configs_t;

#endif // SERIAL_PICO_H