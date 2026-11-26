#include "common/image_codec.h"
#include "common/image_job.h"
#include "common/tcp.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <png.h>
#include <jpeglib.h>

typedef struct {
    struct jpeg_decompress_struct decoder;
    struct jpeg_error_mgr error;
    jmp_buf jump;
    uint8_t *pixels;
    int created;
} jpeg_state;
static void jpeg_failure(j_common_ptr common) {
    jpeg_state *state = (jpeg_state *)((char *)common->err - offsetof(jpeg_state, error));
    longjmp(state->jump, 1); // Recupera el control ante un JPEG invalido sin terminar el servidor.
}
static int decode_jpeg(const uint8_t *data, size_t size, uint8_t **out, uint32_t *width,
                       uint32_t *height) {
    jpeg_state *s = calloc(1, sizeof(*s));
    if (!s) {
        errno = ENOMEM;
        return -1;
    }
    s->decoder.err = jpeg_std_error(&s->error);
    s->error.error_exit = jpeg_failure;
    s->decoder.client_data = s;
    if (setjmp(s->jump)) { // Punto de recuperacion que libera recursos si libjpeg falla.
        if (s->created || s->decoder.mem)
            jpeg_destroy_decompress(&s->decoder);
        free(s->pixels);
        free(s);
        errno = EINVAL;
        return -1;
    }
    jpeg_create_decompress(&s->decoder);
    s->created = 1;
    jpeg_mem_src(&s->decoder, data, (unsigned long)size); // Decodifica desde el buffer TCP, sin guardar archivos compartidos.
    if (jpeg_read_header(&s->decoder, TRUE) != JPEG_HEADER_OK ||
        !image_dimensions_valid(s->decoder.image_width, s->decoder.image_height))
        jpeg_failure((j_common_ptr)&s->decoder);
    s->decoder.out_color_space = JCS_RGB; // Normaliza la salida JPEG a tres canales antes de convertirla a BGR.
    jpeg_start_decompress(&s->decoder);
    if (s->decoder.output_components != 3)
        jpeg_failure((j_common_ptr)&s->decoder);
    size_t stride = (size_t)s->decoder.output_width * 3;
    s->pixels = malloc(stride * s->decoder.output_height);
    if (!s->pixels) {
        jpeg_destroy_decompress(&s->decoder);
        free(s);
        errno = ENOMEM;
        return -1;
    }
    while (s->decoder.output_scanline < s->decoder.output_height) {
        JSAMPROW row = s->pixels + (size_t)s->decoder.output_scanline * stride;
        if (jpeg_read_scanlines(&s->decoder, &row, 1) != 1)
            jpeg_failure((j_common_ptr)&s->decoder);
        for (size_t x = 0; x < stride; x += 3) {
            uint8_t temp = row[x];
            row[x] = row[x + 2]; // Intercambia rojo y azul para la interfaz BGR de los filtros.
            row[x + 2] = temp;
        }
    }
    *width = s->decoder.output_width;
    *height = s->decoder.output_height;
    jpeg_finish_decompress(&s->decoder);
    jpeg_destroy_decompress(&s->decoder);
    *out = s->pixels;
    free(s);
    return 0;
}
int image_decode(const uint8_t *data, size_t size, uint8_t **out, uint32_t *w, uint32_t *h) {
    if (!out || !w || !h) {
        errno = EINVAL;
        return -1;
    }
    *out = NULL;
    *w = *h = 0;
    if (!data || !size || size > TCP_MAX_PAYLOAD) {
        errno = EINVAL;
        return -1;
    }
    if (size >= 2 && data[0] == 0xff && data[1] == 0xd8)
        return decode_jpeg(data, size, out, w, h);
    png_image image = {0};
    image.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_memory(&image, data, size)) {
        png_image_free(&image);
        errno = EINVAL;
        return -1;
    }
    if (!image_dimensions_valid(image.width, image.height)) { // Valida el tamano descomprimido antes de reservar los pixeles.
        png_image_free(&image);
        errno = EMSGSIZE;
        return -1;
    }
    image.format = PNG_FORMAT_BGR; // Los workers reciben pixeles BGR, no objetos ni archivos PNG.
    uint8_t *pixels = malloc(PNG_IMAGE_SIZE(image)); // Reserva solo despues de validar las dimensiones del PNG.
    if (!pixels) {
        png_image_free(&image);
        errno = ENOMEM;
        return -1;
    }
    if (!png_image_finish_read(&image, NULL, pixels, 0, NULL)) {
        free(pixels);
        png_image_free(&image);
        errno = EINVAL;
        return -1;
    }
    *w = image.width;
    *h = image.height;
    *out = pixels;
    png_image_free(&image);
    return 0;
}
int image_encode_png(const uint8_t *pixels, uint32_t w, uint32_t h, unsigned channels,
                     uint8_t **out, size_t *size) {
    if (!out || !size) {
        errno = EINVAL;
        return -1;
    }
    *out = NULL;
    *size = 0;
    if (!pixels || !image_dimensions_valid(w, h) || (channels != 1 && channels != 3)) {
        errno = EINVAL;
        return -1;
    }
    png_image image = {0};
    image.version = PNG_IMAGE_VERSION;
    image.width = w;
    image.height = h;
    image.format = channels == 3 ? PNG_FORMAT_BGR : PNG_FORMAT_GRAY; // Sobel produce un canal; blur y sharpen conservan tres.
    png_alloc_size_t bytes = 0; // La primera llamada consulta cuantos bytes requiere el PNG.
    if (!png_image_write_to_memory(&image, NULL, &bytes, 0, pixels, 0, NULL) ||
        bytes > TCP_MAX_PAYLOAD) {
        png_image_free(&image);
        errno = EMSGSIZE;
        return -1;
    }
    uint8_t *encoded = malloc((size_t)bytes); // La segunda llamada escribira el PNG en este buffer acotado.
    if (!encoded) {
        png_image_free(&image);
        errno = ENOMEM;
        return -1;
    }
    if (!png_image_write_to_memory(&image, encoded, &bytes, 0, pixels, 0, NULL)) {
        free(encoded);
        png_image_free(&image);
        errno = EIO;
        return -1;
    }
    *out = encoded;
    *size = (size_t)bytes;
    png_image_free(&image);
    return 0;
}
