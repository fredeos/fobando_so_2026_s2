#ifndef CLIENT_PROTOCOL_WORKER_H
#define CLIENT_PROTOCOL_WORKER_H
/* 
 * <protocol/worker.h>: esta archivo empaqueta funcionalidades para gestionar
 * un hilo de trabajo dedicado a la comunicacion serial con un dispositivo.
 * 
 * Implementacion: 'src/client/protocol'
*/
// ===========================================================================
// Includes
// ===========================================================================
// + Libraries
#include <pthread.h>

// + C libraries
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// + Modules
#include "common/protocol.h"
#include "serial/posix.h"
#include "serial/serial.h"

// ===========================================================================
// Definiciones
// ===========================================================================

// + Gestion de protocolo
#define PROTOCOL_MESSAGE_LEN 128u  // longitud maxima de mensajes(payload)
#define PROTOCOL_QUEUE_SIZE  5u    // tamaño maximo de cola

// ===========================================================================
// Declaraciones y tipos
// ===========================================================================
#ifdef __cplusplus
extern "C" {
#endif
// ---------------------------------------------------------------------------
// + Worker misc

/* Estructura de datos tipo cola para almacenar mensajes */
typedef struct {
    pthread_mutex_t mutex;  // primitiva de sincronizacion
    char messages[PROTOCOL_QUEUE_SIZE][PROTOCOL_MESSAGE_LEN]; // cola de mensajes
    uint8_t priority[PROTOCOL_QUEUE_SIZE];  // cola de prioridades
    int16_t last;   // ultima posicion usada
    bool available; // mensajes disponibles
} protocol_queue_t;

/* Estructura de datos para empaquetar informacion de un trabajador */
typedef struct {
    /* Informacion del worker */
    uint8_t id; // id del worker
    const char* device; // puerto del dispositivo
    /* Banderas de control */
    bool debug;   // bandera de debug
    bool running; // worker en ejecucion
    bool online;  // conexion activa
    bool kill;    // señal de terminacion
    /* Valores de control */
    uint64_t timeout_ms; // timeout para resincronizar conexion
    uint8_t criteria;    // criterio de aceptacion de mensajes
    /* Colas de mensajes */
    protocol_queue_t in;    // mensajes recibidos
    protocol_queue_t out;   // mensajes por enviar
    /* Interfaz serial */
    int fd;
    posix_serial_config_t config;
    /* Interfaz de protocolo */
    // [TX:output]
    protocol_frame_t tx_frame;
    protocol_tx_t tx;
    serial_packet_t tx_packet;
    // [RX:input]
    protocol_frame_t rx_frame;
    protocol_rx_t rx;
    serial_packet_t rx_packet;
    /* Sincronizacion */
    pthread_mutex_t mutex;  // primitiva de sincronizacion
} protocol_worker_t;

// ---------------------------------------------------------------------------
// + Worker Queues

/* Agregar elemento a la cola */
int pqueue_append(const void *src, size_t length, uint8_t priority, protocol_queue_t* queue);

/* Consumir un elemento de la cola */
int pqueue_consume(void *dst, size_t length, uint8_t *priority, protocol_queue_t* queue);

// ---------------------------------------------------------------------------
// + Worker

/* Inicializa las propiedades del worker */
int init_pworker(
    uint8_t id, 
    const char* device,
    bool debug,
    uint64_t timeout_ms,
    uint8_t criteria,
    protocol_worker_t* worker
);

/* Inicia el hilo de comunicacion del worker */
int start_pworker(protocol_worker_t* worker);

/* Terminar el hilo de comunicacion del worker */
int kill_pworker(protocol_worker_t* worker);

/* Rutina de comunicacion con dispositivo serial */
void* protocol_worker(void *arg);

// ---------------------------------------------------------------------------
#ifdef __cplusplus
}
#endif
// ===========================================================================
#endif // CLIENT_PROTOCOL_WORKER_H