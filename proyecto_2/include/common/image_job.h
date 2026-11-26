#ifndef DIPS_IMAGE_JOB_H
#define DIPS_IMAGE_JOB_H
#include <stddef.h>
#include <stdint.h>

#define IMAGE_JOB_HEADER_SIZE 32u // Ocho campos de 32 bits serializados en orden de red.
#define IMAGE_MAX_BYTES (32u * 1024u * 1024u) // Limite de pixeles decodificados, distinto al limite TCP.
#define IMAGE_MAX_DIMENSION 32768u
typedef struct {
    uint32_t filter, width, height, block_rows, blocks, radius, intensity; // Describe la operacion solicitada por el cliente.
} image_job;

/* Explicit versioned big-endian request. Zero width/height means original
 * dimensions only when allow_original is true. Returns 0 or -1/errno. */
int image_job_valid(const image_job *job, int allow_original);
static inline int image_dimensions_valid(uint32_t width, uint32_t height) {
    return width && height && width <= IMAGE_MAX_DIMENSION && height <= IMAGE_MAX_DIMENSION &&
           (size_t)width * height <= IMAGE_MAX_BYTES / 3; // Se reservan tres canales BGR por pixel.
}
int image_job_encode(const image_job *job, uint8_t out[IMAGE_JOB_HEADER_SIZE]);
int image_job_decode(const uint8_t *data, size_t size, image_job *job);
#endif
