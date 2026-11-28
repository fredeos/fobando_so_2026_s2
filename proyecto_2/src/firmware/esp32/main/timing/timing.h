#ifndef ESP_TIMING_H
#define ESP_TIMING_H

#include "esp_timer.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Obtener tiempo actual en el ESP32 */
uint64_t get_time_ms(void);

#ifdef __cplusplus
}
#endif

#endif