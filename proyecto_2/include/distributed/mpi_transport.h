#ifndef DIPS_MPI_TRANSPORT_H
#define DIPS_MPI_TRANSPORT_H

#include <mpi.h>
#include <stddef.h>
#include <stdint.h>
#include "common/crypto.h"

#define MPI_TRANSPORT_MAX_PAYLOAD (64u * 1024u * 1024u) // Limite del contenido binario de cada mensaje MPI.
enum mpi_message_type {
    MPI_MSG_REGISTER = 1, MPI_MSG_READY, MPI_MSG_TASK,
    MPI_MSG_RESULT, MPI_MSG_ERROR, MPI_MSG_STOP
};

typedef struct {
    MPI_Comm comm; // Comunicador usado por coordinador y trabajadores.
    int rank, size; // Identificador local y cantidad real de procesos lanzados por mpirun.
    uint8_t key[CRYPTO_KEY_SIZE];
    uint64_t *sent, *received; // Secuencias por interlocutor para rechazar mensajes repetidos.
} mpi_transport;

typedef struct {
    unsigned type;
    int source; // Origen real confirmado mediante MPI_Status y el encabezado autenticado.
    size_t size;
    uint8_t *data; // Payload ya autenticado; nunca contiene punteros transmitidos.
} mpi_message;

/* Explicit unsigned big-endian encoding. Never send native structs/pointers. */
void mpi_put_u32(uint8_t *out, uint32_t value);
uint32_t mpi_get_u32(const uint8_t *in);
void mpi_put_u64(uint8_t *out, uint64_t value);
uint64_t mpi_get_u64(const uint8_t *in);

/* Loads DIPS_AES_KEY, initializes per-peer authenticated sequence counters.
 * Functions return 0 or -1/errno. Callers must abort MPI on transport errors. */
int mpi_transport_init(mpi_transport *transport, MPI_Comm comm);
void mpi_transport_destroy(mpi_transport *transport);
int mpi_transport_send(mpi_transport *transport, int destination,
                       unsigned type, const void *payload, size_t size);
/* source can be MPI_ANY_SOURCE. out must be empty. Returns authenticated
 * content only; release it with mpi_message_free(), including empty messages.
 * Wire: nonce[12] | GCM ciphertext | tag[16]. Encrypted plaintext begins with
 * version/type/source/destination:u32, sequence:u64, payload length:u32.
 * AAD is the constant protocol identifier DIPSMPI1. MPI tag is fixed at 2601.
 * Destination, MPI source, type, exact length and sequence are validated. */
int mpi_transport_recv(mpi_transport *transport, int source, mpi_message *out);
void mpi_message_free(mpi_message *message);

#endif
