#include "common/tcp.h"

#include <arpa/inet.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

void tcp_close(int *fd)
{
    int saved_errno = errno;
    if (fd != NULL && *fd >= 0) {
        close(*fd);
        *fd = -1;
    }
    errno = saved_errno;
}

int tcp_server_create(uint16_t port, int backlog)
{
    int fd, reuse = 1;
    struct sockaddr_in address = {0};

    if (port == 0 || backlog <= 0) {
        errno = EINVAL;
        return -1;
    }
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0 ||
        bind(fd, (const struct sockaddr *)&address, sizeof(address)) < 0 ||
        listen(fd, backlog) < 0) {
        tcp_close(&fd);
        return -1;
    }
    return fd;
}

int tcp_accept(int server_fd)
{
    int fd;
    do {
        fd = accept(server_fd, NULL, NULL);
    } while (fd < 0 && errno == EINTR);
    return fd;
}

int tcp_connect(const char *ipv4, uint16_t port)
{
    struct sockaddr_in address = {0};
    int fd;

    if (ipv4 == NULL || port == 0) {
        errno = EINVAL;
        return -1;
    }
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (inet_pton(AF_INET, ipv4, &address.sin_addr) != 1) {
        errno = EINVAL;
        return -1;
    }
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    if (connect(fd, (const struct sockaddr *)&address, sizeof(address)) < 0) {
        /* Ante EINTR tampoco reutilizamos una conexión de estado incierto. */
        tcp_close(&fd);
        return -1;
    }
    return fd;
}

int tcp_send_all(int fd, const void *buffer, size_t size)
{
    const uint8_t *bytes = buffer;
    if (size != 0 && buffer == NULL) {
        errno = EINVAL;
        return -1;
    }
    while (size != 0) {
        size_t chunk = size > INT_MAX ? INT_MAX : size;
        ssize_t sent = send(fd, bytes, chunk, MSG_NOSIGNAL);
        if (sent < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (sent == 0) {
            errno = EPIPE;
            return -1;
        }
        bytes += sent;
        size -= (size_t)sent;
    }
    return 0;
}

int tcp_recv_all(int fd, void *buffer, size_t size)
{
    uint8_t *bytes = buffer;
    if (size != 0 && buffer == NULL) {
        errno = EINVAL;
        return -1;
    }
    while (size != 0) {
        size_t chunk = size > INT_MAX ? INT_MAX : size;
        ssize_t received = recv(fd, bytes, chunk, 0);
        if (received < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (received == 0) {
            errno = ECONNRESET;
            return -1;
        }
        bytes += received;
        size -= (size_t)received;
    }
    return 0;
}

static int valid_type(uint16_t type)
{
    return type == TCP_MESSAGE_IMAGE || type == TCP_MESSAGE_ACK;
}

int tcp_send_message(int fd, const uint8_t key[CRYPTO_KEY_SIZE],
                     uint16_t type, const void *payload, size_t payload_size)
{
    uint8_t header[TCP_HEADER_SIZE], nonce[CRYPTO_NONCE_SIZE];
    uint8_t tag[CRYPTO_TAG_SIZE], *ciphertext = NULL;
    uint16_t version_be = htons(TCP_PROTOCOL_VERSION), type_be = htons(type);
    uint32_t size_be;
    int result = -1, saved_errno;

    if (key == NULL || !valid_type(type) ||
        (payload_size != 0 && payload == NULL)) {
        errno = EINVAL;
        return -1;
    }
    if (payload_size > TCP_MAX_PAYLOAD) {
        errno = EMSGSIZE;
        return -1;
    }
    size_be = htonl((uint32_t)payload_size);
    memcpy(header, &version_be, sizeof(version_be));
    memcpy(header + 2, &type_be, sizeof(type_be));
    memcpy(header + 4, &size_be, sizeof(size_be));
    if (crypto_encrypt(key, payload, payload_size, header, sizeof(header),
                       nonce, &ciphertext, tag) < 0) return -1;
    if (tcp_send_all(fd, header, sizeof(header)) == 0 &&
        tcp_send_all(fd, nonce, sizeof(nonce)) == 0 &&
        tcp_send_all(fd, ciphertext, payload_size) == 0 &&
        tcp_send_all(fd, tag, sizeof(tag)) == 0) result = 0;
    saved_errno = errno;
    free(ciphertext);
    errno = saved_errno;
    return result;
}

int tcp_recv_message(int fd, const uint8_t key[CRYPTO_KEY_SIZE],
                     tcp_message *out)
{
    uint8_t header[TCP_HEADER_SIZE], nonce[CRYPTO_NONCE_SIZE];
    uint8_t tag[CRYPTO_TAG_SIZE], *ciphertext = NULL, *plaintext = NULL;
    uint16_t version_be, type_be, type;
    uint32_t size_be, size;
    int saved_errno;

    if (out == NULL) {
        errno = EINVAL;
        return -1;
    }
    *out = (tcp_message){0};
    if (key == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (tcp_recv_all(fd, header, sizeof(header)) < 0) return -1;
    memcpy(&version_be, header, sizeof(version_be));
    memcpy(&type_be, header + 2, sizeof(type_be));
    memcpy(&size_be, header + 4, sizeof(size_be));
    type = ntohs(type_be);
    size = ntohl(size_be);
    /* Validar antes de reservar memoria; aún no confiamos en estos campos. */
    if (ntohs(version_be) != TCP_PROTOCOL_VERSION || !valid_type(type)) {
        errno = EPROTO;
        return -1;
    }
    if (size > TCP_MAX_PAYLOAD) {
        errno = EMSGSIZE;
        return -1;
    }
    ciphertext = malloc(size == 0 ? 1 : size);
    if (ciphertext == NULL) {
        errno = ENOMEM;
        return -1;
    }
    if (tcp_recv_all(fd, nonce, sizeof(nonce)) < 0 ||
        tcp_recv_all(fd, ciphertext, size) < 0 ||
        tcp_recv_all(fd, tag, sizeof(tag)) < 0 ||
        crypto_decrypt(key, ciphertext, size, header, sizeof(header),
                       nonce, tag, &plaintext) < 0) {
        saved_errno = errno;
        free(ciphertext);
        errno = saved_errno;
        return -1;
    }
    free(ciphertext);
    out->type = type;
    out->payload_size = size;
    out->payload = plaintext;
    return 0;
}

void tcp_message_free(tcp_message *message)
{
    if (message != NULL) {
        crypto_clear(message->payload, message->payload_size);
        free(message->payload);
        *message = (tcp_message){0};
    }
}
