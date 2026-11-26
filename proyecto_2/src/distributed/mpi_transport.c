#include "distributed/mpi_transport.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define WIRE_HEADER 28u // Campos internos cifrados: version, tipo, ranks, secuencia y longitud.
#define WIRE_OVERHEAD (WIRE_HEADER + CRYPTO_NONCE_SIZE + CRYPTO_TAG_SIZE)
#define MESSAGE_TAG 2601 // Etiqueta MPI que distingue las tramas de este protocolo.
static const uint8_t aad[] = {'D','I','P','S','M','P','I','1'}; // Vincula cada autenticacion GCM con esta version del protocolo MPI.

void mpi_put_u32(uint8_t *out, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) out[i] = (uint8_t)(value >> (24 - 8 * i)); // Serializa sin depender del endianess ni del padding del equipo.
}
uint32_t mpi_get_u32(const uint8_t *in)
{
    uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i) value = (value << 8) | in[i];
    return value;
}
void mpi_put_u64(uint8_t *out, uint64_t value)
{
    for (unsigned i = 0; i < 8; ++i) out[i] = (uint8_t)(value >> (56 - 8 * i)); // Codifica explicitamente secuencias y tiempos de 64 bits.
}
uint64_t mpi_get_u64(const uint8_t *in)
{
    uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i) value = (value << 8) | in[i];
    return value;
}
static int allowed(int source, int destination, unsigned type)
{
    if (source < 0 || destination < 0 || source == destination) return 0;
    if (type == MPI_MSG_STOP || type == MPI_MSG_ERROR)
        return source == 0 || destination == 0;
    if (source == 0) return type == MPI_MSG_TASK; // Solo el coordinador puede asignar tareas a los trabajadores.
    return destination == 0 && (type == MPI_MSG_REGISTER || type == MPI_MSG_READY ||
                               type == MPI_MSG_RESULT);
}

int mpi_transport_init(mpi_transport *t, MPI_Comm comm)
{
    if (!t) { errno = EINVAL; return -1; }
    *t = (mpi_transport){0};
    t->comm = comm;
    if (MPI_Comm_rank(comm, &t->rank) != MPI_SUCCESS ||
        MPI_Comm_size(comm, &t->size) != MPI_SUCCESS) { errno = EIO; return -1; }
    if (t->size < 2 || crypto_load_key(t->key) < 0) { errno = EINVAL; return -1; } // Todos los ranks deben usar la misma clave valida del entorno.
    t->sent = calloc((size_t)t->size, sizeof(*t->sent)); // Mantiene una secuencia independiente por destino.
    t->received = calloc((size_t)t->size, sizeof(*t->received));
    if (!t->sent || !t->received) {
        mpi_transport_destroy(t); errno = ENOMEM; return -1;
    }
    return 0;
}
void mpi_transport_destroy(mpi_transport *t)
{
    if (t) {
        free(t->sent); free(t->received);
        crypto_clear(t->key, sizeof(t->key)); // Borra la clave al destruir el transporte.
        *t = (mpi_transport){0};
    }
}

int mpi_transport_send(mpi_transport *t, int dest, unsigned type,
                       const void *payload, size_t size)
{
    if (!t || !t->sent || dest < 0 || dest >= t->size ||
        !allowed(t->rank, dest, type) || (size && !payload)) { errno = EINVAL; return -1; }
    if (size > MPI_TRANSPORT_MAX_PAYLOAD || t->sent[dest] == UINT64_MAX) {
        errno = EMSGSIZE; return -1;
    }
    size_t length = WIRE_HEADER + size;
    uint8_t *plain = malloc(length), *wire = malloc(length + 28), *cipher = NULL;
    if (!plain || !wire) { free(plain); free(wire); errno = ENOMEM; return -1; }
    mpi_put_u32(plain, 1); mpi_put_u32(plain + 4, type); // La version y el tipo tambien quedan dentro del contenido cifrado.
    mpi_put_u32(plain + 8, (uint32_t)t->rank); mpi_put_u32(plain + 12, (uint32_t)dest); // Autentica tanto el emisor declarado como el destinatario.
    mpi_put_u64(plain + 16, t->sent[dest] + 1); mpi_put_u32(plain + 24, (uint32_t)size); // Incluye la siguiente secuencia y la longitud exacta del payload.
    if (size) memcpy(plain + WIRE_HEADER, payload, size);
    int result = crypto_encrypt(t->key, plain, length, aad, sizeof(aad), wire,
                                &cipher, wire + CRYPTO_NONCE_SIZE + length); // Cifra todo el encabezado interno y el contenido con AES-GCM.
    if (result == 0) {
        memcpy(wire + CRYPTO_NONCE_SIZE, cipher, length);
        if (MPI_Send(wire, (int)(length + 28), MPI_BYTE, dest, MESSAGE_TAG, t->comm) != MPI_SUCCESS) { // Transmite bytes serializados, nunca una estructura nativa.
            errno = EIO; result = -1;
        } else ++t->sent[dest]; // Avanza la secuencia solo cuando MPI confirma el envio.
    }
    int saved = errno;
    crypto_clear(plain, length); free(plain); free(wire); free(cipher);
    errno = saved;
    return result;
}

int mpi_transport_recv(mpi_transport *t, int source, mpi_message *out)
{
    if (!out) { errno = EINVAL; return -1; }
    *out = (mpi_message){0};
    if (!t || !t->received || (source != MPI_ANY_SOURCE &&
        (source < 0 || source >= t->size || source == t->rank))) { errno = EINVAL; return -1; }
    MPI_Status status;
    int count;
    if (MPI_Probe(source, MESSAGE_TAG, t->comm, &status) != MPI_SUCCESS || // Consulta el origen y tamano antes de reservar el buffer de recepcion.
        MPI_Get_count(&status, MPI_BYTE, &count) != MPI_SUCCESS) { errno = EIO; return -1; }
    if (count < (int)WIRE_OVERHEAD || count > (int)(MPI_TRANSPORT_MAX_PAYLOAD + WIRE_OVERHEAD) || // Rechaza tramas truncadas o excesivas antes de descifrarlas.
        status.MPI_SOURCE < 0 || status.MPI_SOURCE >= t->size || status.MPI_SOURCE == t->rank) {
        errno = EPROTO; return -1;
    }
    size_t length = (size_t)count - CRYPTO_NONCE_SIZE - CRYPTO_TAG_SIZE;
    uint8_t *wire = malloc((size_t)count), *plain = NULL;
    if (!wire) { errno = ENOMEM; return -1; }
    if (MPI_Recv(wire, count, MPI_BYTE, status.MPI_SOURCE, MESSAGE_TAG, t->comm,
                 MPI_STATUS_IGNORE) != MPI_SUCCESS) { free(wire); errno = EIO; return -1; } // Recibe exactamente la trama identificada por Probe.
    if (crypto_decrypt(t->key, wire + CRYPTO_NONCE_SIZE, length, aad, sizeof(aad),
                       wire, wire + CRYPTO_NONCE_SIZE + length, &plain) < 0) { // Un tag invalido impide utilizar cualquier campo del mensaje.
        int saved = errno; free(wire); errno = saved; return -1;
    }
    free(wire);
    uint32_t type = mpi_get_u32(plain + 4), sender = mpi_get_u32(plain + 8);
    size_t size = mpi_get_u32(plain + 24);
    uint64_t sequence = mpi_get_u64(plain + 16);
    int valid = mpi_get_u32(plain) == 1 && sender == (uint32_t)status.MPI_SOURCE && // El rank declarado debe coincidir con el origen real de MPI.
        mpi_get_u32(plain + 12) == (uint32_t)t->rank &&
        allowed(status.MPI_SOURCE, t->rank, type) && size == length - WIRE_HEADER &&
        t->received[sender] != UINT64_MAX && sequence == t->received[sender] + 1; // Rechaza repeticiones y secuencias fuera del orden esperado.
    if (!valid) { crypto_clear(plain, length); free(plain); errno = EPROTO; return -1; }
    memmove(plain, plain + WIRE_HEADER, size); // Quita el encabezado solo despues de autenticar y validar toda la trama.
    crypto_clear(plain + size, length - size); // Borra los bytes residuales del encabezado interno.
    t->received[sender] = sequence;
    out->type = type; out->source = status.MPI_SOURCE; out->size = size; out->data = plain;
    return 0;
}

void mpi_message_free(mpi_message *message)
{
    if (message) {
        crypto_clear(message->data, message->size); free(message->data);
        *message = (mpi_message){0};
    }
}
