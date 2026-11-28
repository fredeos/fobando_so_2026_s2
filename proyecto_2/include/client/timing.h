#ifndef CLIENT_TIMING_H
#define CLIENT_TIMING_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Obtener el tiempo en milisegundos */
uint64_t get_time_ms(void);

#ifdef __cplusplus
}
#endif

#endif // CLIENT_TIMING_H