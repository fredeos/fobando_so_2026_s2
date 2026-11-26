#ifndef DIPS_IMAGE_CODEC_H
#define DIPS_IMAGE_CODEC_H
#include <stddef.h>
#include <stdint.h>
/* Decode PNG/JPEG in memory to packed BGR; output is malloc-owned. PNG results
 * use 1 (Sobel) or 3 (BGR) channels. All limits checked before pixel allocation. */
int image_decode(const uint8_t *encoded, size_t size, uint8_t **bgr, uint32_t *width,
                 uint32_t *height); // Entrega los pixeles decodificados en memoria al coordinador.
int image_encode_png(const uint8_t *pixels, uint32_t width, uint32_t height, unsigned channels,
                     uint8_t **encoded, size_t *size); // Prepara un PNG binario para devolverlo por TCP.
#endif
