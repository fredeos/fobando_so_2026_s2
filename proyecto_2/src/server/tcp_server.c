#include "server/header.h"
#include "common/tcp.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

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

static void handle_client(int fd, const uint8_t key[CRYPTO_KEY_SIZE])
{
    const uint8_t ack[] = {0x4f, 0x4b};
    tcp_message message = {0};

    if (tcp_recv_message(fd, key, &message) < 0) {
        perror("Mensaje rechazado: recepción/autenticación fallida");
        return;
    }
    if (message.type != TCP_MESSAGE_IMAGE || message.payload_size == 0) {
        fprintf(stderr, "Mensaje rechazado: se esperaba IMAGE no vacía.\n");
        tcp_message_free(&message);
        return;
    }
    printf("Imagen autenticada recibida: %u bytes.\n",
           (unsigned int)message.payload_size);
    fflush(stdout);
    tcp_message_free(&message);
    if (tcp_send_message(fd, key, TCP_MESSAGE_ACK, ack, sizeof(ack)) < 0)
        perror("No se pudo enviar el ACK");
}

int tcp_server_run(int argc, char **argv)
{
    uint8_t key[CRYPTO_KEY_SIZE] = {0};
    uint16_t port;
    int server_fd = -1, result = EXIT_FAILURE;

    if (argc != 2 || parse_port(argv[1], &port) < 0) {
        fprintf(stderr, "Uso: %s <PUERTO 1..65535>\n", argv[0]);
        return EXIT_FAILURE;
    }
    if (crypto_load_key(key) < 0) {
        fprintf(stderr, "DIPS_AES_KEY debe contener 64 dígitos hexadecimales.\n");
        goto cleanup;
    }
    server_fd = tcp_server_create(port, 16);
    if (server_fd < 0) {
        perror("No se pudo iniciar el servidor TCP");
        goto cleanup;
    }
    printf("Servidor TCP IPv4 escuchando en el puerto %u.\n", (unsigned int)port);
    fflush(stdout);
    for (;;) {
        int client_fd = tcp_accept(server_fd);
        if (client_fd < 0) {
            if (errno == ECONNABORTED) continue;
            perror("No se pudo aceptar una conexión");
            break;
        }
        handle_client(client_fd, key);
        tcp_close(&client_fd);
    }

cleanup:
    tcp_close(&server_fd);
    crypto_clear(key, sizeof(key));
    return result;
}
