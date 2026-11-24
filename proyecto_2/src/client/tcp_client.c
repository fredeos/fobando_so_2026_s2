#include "client/header.h"
#include "common/tcp.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_port(const char *text, uint16_t *port)
{
    unsigned int value = 0;
    if (*text == '\0') return -1;
    for (const char *p = text; *p != '\0'; ++p) {
        if (*p < '0' || *p > '9') return -1;
        value = value * 10 + (unsigned int)(*p - '0');
        if (value > UINT16_MAX) return -1;
    }
    if (value == 0) return -1;
    *port = (uint16_t)value;
    return 0;
}

static int read_image(const char *path, uint8_t **data, size_t *size)
{
    FILE *file = fopen(path, "rb");
    uint8_t *buffer = NULL;
    long length;
    int error = EIO;

    *data = NULL;
    *size = 0;
    if (file == NULL) return -1;
    if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0)
        goto fail;
    if (length == 0 || (unsigned long)length > TCP_MAX_PAYLOAD) {
        error = EMSGSIZE;
        goto fail;
    }
    if (fseek(file, 0, SEEK_SET) != 0) goto fail;
    buffer = malloc((size_t)length);
    if (buffer == NULL) {
        error = ENOMEM;
        goto fail;
    }
    if (fread(buffer, 1, (size_t)length, file) != (size_t)length ||
        fgetc(file) != EOF || ferror(file)) goto fail;
    if (fclose(file) != 0) {
        file = NULL;
        goto fail;
    }
    *data = buffer;
    *size = (size_t)length;
    return 0;

fail:
    if (file != NULL) fclose(file);
    if (buffer != NULL) crypto_clear(buffer, (size_t)length);
    free(buffer);
    errno = error;
    return -1;
}

int tcp_client_run(int argc, char **argv)
{
    const uint8_t expected_ack[] = {0x4f, 0x4b};
    uint8_t key[CRYPTO_KEY_SIZE] = {0}, *image = NULL;
    size_t image_size = 0;
    uint16_t port;
    int fd = -1, result = EXIT_FAILURE;
    tcp_message ack = {0};

    if (argc != 4 || parse_port(argv[2], &port) < 0) {
        fprintf(stderr, "Uso: %s <IP_SERVIDOR> <PUERTO 1..65535> <RUTA_IMAGEN>\n",
                argv[0]);
        return EXIT_FAILURE;
    }
    if (crypto_load_key(key) < 0) {
        fprintf(stderr, "DIPS_AES_KEY debe contener 64 dígitos hexadecimales.\n");
        goto cleanup;
    }
    if (read_image(argv[3], &image, &image_size) < 0) {
        perror("No se pudo leer la imagen (tamaño válido: 1 byte a 8 MiB)");
        goto cleanup;
    }
    fd = tcp_connect(argv[1], port);
    if (fd < 0) {
        perror("Conexión IPv4 fallida");
        goto cleanup;
    }
    if (tcp_send_message(fd, key, TCP_MESSAGE_IMAGE, image, image_size) < 0) {
        perror("No se pudo enviar la imagen");
        goto cleanup;
    }
    if (tcp_recv_message(fd, key, &ack) < 0) {
        perror("No se pudo recibir/autenticar el ACK");
        goto cleanup;
    }
    if (ack.type != TCP_MESSAGE_ACK || ack.payload_size != sizeof(expected_ack) ||
        memcmp(ack.payload, expected_ack, sizeof(expected_ack)) != 0) {
        fprintf(stderr, "Respuesta inesperada: se esperaba un ACK de éxito.\n");
        goto cleanup;
    }
    printf("Éxito: imagen de %zu bytes enviada y ACK autenticado.\n", image_size);
    result = EXIT_SUCCESS;

cleanup:
    tcp_message_free(&ack);
    tcp_close(&fd);
    crypto_clear(image, image_size);
    free(image);
    crypto_clear(key, sizeof(key));
    return result;
}
