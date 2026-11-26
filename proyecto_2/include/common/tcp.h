#ifndef DIPS_TCP_H
#define DIPS_TCP_H

#include <stddef.h>
#include <stdint.h>

#include "common/crypto.h"

#define TCP_PROTOCOL_VERSION 1u
#define TCP_HEADER_SIZE 8u // Version, tipo y longitud; este encabezado tambien se autentica.
#define TCP_MAX_PAYLOAD (8u * 1024u * 1024u) // Limite por mensaje para acotar las reservas de memoria.

enum tcp_message_type {
    TCP_MESSAGE_IMAGE = 1,
    TCP_MESSAGE_ACK = 2,
    TCP_MESSAGE_PROCESS = 3,
    TCP_MESSAGE_RESULT = 4,
    TCP_MESSAGE_ERROR = 5
};

typedef struct {
    uint16_t type;
    uint32_t payload_size; // Cantidad de bytes binarios, sin terminador de cadena.
    uint8_t *payload; // Contenido autenticado; se libera con tcp_message_free().
} tcp_message;

/* Formato en la red, sin padding:
 * version:u16 BE | tipo:u16 BE | longitud:u32 BE | nonce:12 bytes |
 * ciphertext:longitud bytes | tag:16 bytes.
 * Los 8 bytes originales del encabezado son el AAD de AES-256-GCM.
 * Longitud máxima: 8 MiB; el cifrado conserva la longitud del payload.
 * ACK de esta aplicación: dos bytes 0x4f, 0x4b ("OK").
 * Proceso integrado: PROCESS(image_job) + IMAGE(PNG/JPEG) -> RESULT(PNG)
 * o ERROR(texto). Cada mensaje se cifra/autentica independientemente.
 * Las funciones devuelven -1 con errno al fallar, salvo tcp_close/free.
 */

/* Crea un listener IPv4 en INADDR_ANY. Devuelve su descriptor o -1. */
int tcp_server_create(uint16_t port, int backlog);

/* Acepta un cliente, reintentando EINTR. Devuelve su descriptor o -1. */
int tcp_accept(int server_fd);

/* Conecta a una IPv4 numérica (sin DNS). Devuelve el descriptor o -1. */
int tcp_connect(const char *ipv4, uint16_t port);

/* Transfiere exactamente size bytes; éxito: 0. Maneja parciales y EINTR.
 * EOF antes de completar produce ECONNRESET. El envío evita SIGPIPE.
 * Una transferencia fallida obliga a cerrar la conexión. */
int tcp_send_all(int fd, const void *buffer, size_t size);
int tcp_recv_all(int fd, void *buffer, size_t size);

/* Cifra y transmite un mensaje completo. Éxito: 0. */
int tcp_send_message(int fd, const uint8_t key[CRYPTO_KEY_SIZE],
                     uint16_t type, const void *payload, size_t payload_size);

/* Recibe y autentica un mensaje. Éxito: 0; payload pertenece al llamador.
 * out debe estar vacío; ante un error queda vacío. Libérelo con
 * tcp_message_free(). Encabezados inválidos producen EPROTO/EMSGSIZE. */
int tcp_recv_message(int fd, const uint8_t key[CRYPTO_KEY_SIZE],
                     tcp_message *out);

/* Borra y libera el payload, y deja el mensaje vacío. Acepta NULL. */
void tcp_message_free(tcp_message *message);

/* Cierra una sola vez (también ante EINTR en Linux) y deja *fd en -1.
 * Preserva errno; acepta NULL o un descriptor ya invalidado. */
void tcp_close(int *fd);

#endif
