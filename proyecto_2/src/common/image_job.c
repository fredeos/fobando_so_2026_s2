#include "common/image_job.h"
#include "filters/filters.h"
#include <arpa/inet.h>
#include <errno.h>
#include <string.h>

int image_job_valid(const image_job *j, int original) { // Centraliza los limites antes de aceptar o distribuir una solicitud.
    if (!j || !filters_valid(j->filter, j->radius, j->intensity) ||
        (!image_dimensions_valid(j->width, j->height) &&
         !(original && j->width == 0 && j->height == 0)) ||
        (j->blocks ? j->block_rows != 0 || (j->height && j->blocks > j->height)
                   : j->block_rows == 0)) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}
int image_job_encode(const image_job *j, uint8_t out[IMAGE_JOB_HEADER_SIZE]) {
    if (!out || image_job_valid(j, 1) < 0) {
        errno = EINVAL;
        return -1;
    }
    const uint32_t fields[] = {1,         j->filter, j->width,    j->height, j->block_rows,
                               j->blocks, j->radius, j->intensity};
    for (unsigned i = 0; i < 8; ++i) {
        uint32_t value = htonl(fields[i]); // Cada parametro viaja como entero de 32 bits en orden de red.
        memcpy(out + 4 * i, &value, 4); // Serializa campos; no transmite la estructura nativa de C.
    }
    return 0;
}
int image_job_decode(const uint8_t *data, size_t size, image_job *j) {
    if (!data || !j || size != IMAGE_JOB_HEADER_SIZE) {
        errno = EPROTO;
        return -1;
    }
    uint32_t fields[8];
    for (unsigned i = 0; i < 8; ++i) {
        memcpy(&fields[i], data + 4 * i, 4);
        fields[i] = ntohl(fields[i]); // Decodifica los parametros recibidos por TCP.
    }
    if (fields[0] != 1) { // Rechaza solicitudes con una version de formato incompatible.
        errno = EPROTO;
        return -1;
    }
    *j = (image_job){fields[1], fields[2], fields[3], fields[4], fields[5], fields[6], fields[7]};
    return image_job_valid(j, 1); // El servidor resolvera dimensiones cero con la imagen original.
}
