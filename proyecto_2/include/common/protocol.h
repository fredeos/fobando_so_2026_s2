#ifndef COMMON_PROTOCOL_H
#define COMMON_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

#include "serial/serial.h"

/* Identificadores del inicio de trama */
/* El inicio de trama se identifica al encontrar una secuencia de bytes alteranada,
 * todos los bytes leidos antes del inicio son descartados.
 * ej. 'A5 5A' es un inicio de trama valido para 2 bytes
 * La lectura del inicio de trama debe ser byte a byte, despues de eso se pueden leer bytes
 * en paquetes para cada componente.
*/
#define PROTOCOL_MAGIC_0 0xA5u
#define PROTOCOL_MAGIC_1 0x5Au

/* Versión del protocolo */
#define PROTOCOL_VERSION 1u

/* Tamaños de los componentes de una trama */
#define PROTOCOL_HEADER_BYTES    2u
#define PROTOCOL_VERSION_BYTES   1u
#define PROTOCOL_RELEVANCE_BYTES 1u
#define PROTOCOL_LENGTH_BYTES    4u
#define PROTOCOL_PAYLOAD_BYTES   256u
#define PROTOCOL_CRC_BYTES       2u

/* Offsets de los componentes de la trama */
#define PROTOCOL_HEADER_OFFSET    0u
#define PROTOCOL_VERSION_OFFSET   PROTOCOL_HEADER_BYTES
#define PROTOCOL_RELEVANCE_OFFSET \
    (PROTOCOL_VERSION_OFFSET + PROTOCOL_VERSION_BYTES)
#define PROTOCOL_LENGTH_OFFSET \
    (PROTOCOL_RELEVANCE_OFFSET + PROTOCOL_RELEVANCE_BYTES)
#define PROTOCOL_PAYLOAD_OFFSET  \
    (PROTOCOL_LENGTH_OFFSET + PROTOCOL_LENGTH_BYTES)
#define PROTOCOL_CRC_OFFSET \
    (PROTOCOL_PAYLOAD_OFFSET + PROTOCOL_PAYLOAD_BYTES)

/* Tamaños de una trama */
#define PROTOCOL_FRAME_BYTES \
    (PROTOCOL_CRC_OFFSET + PROTOCOL_CRC_BYTES)

/* Confirmacion de trama */
/* La confirmacion de trama requiere obtener una secuencia de bytes alternada. Todos
 * los bytes antes de la confirmacion son descartados.
 * ej. 'E7 7E' es ACK valido para 2 bytes
 * ej. 'D9 9D' es NACK valido para 2 bytes
 * La confirmacion de trama indica la recepcion correcta de trama. La lectura
 * de esta debe hacerse byte a byte. La escritura puede hacerse completa
*/
#define PROTOCOL_ACK_BYTES 4u

#define PROTOCOL_ACK_0 0xE7u
#define PROTOCOL_ACK_1 0x7Eu

#define PROTOCOL_NACK_0 0xD9u
#define PROTOCOL_NACK_1 0x9Du

#define PROTOCOL_MAX_ATTEMPTS 300

/* NOTA: la identificacion de inicio de trama y confirmacion de protocolo sirven como una sincronizacion
 * entre los dispositivos
*/

/*
 * Tipos de errores para operaciones del protocolo
 *
*/
typedef enum {
    PROTOCOL_OK = 0,
    // errores generales
    PROTOCOL_ERROR = -1,
    PROTOCOL_OUT_RANGE    = -2,
    PROTOCOL_INVALID_ARG  = -3,
    PROTOCOL_INVALID_SIZE = -4,
    PROTOCOL_INVALID_MODE = -5,
    PROTOCOL_SERIAL_EIO = -6
} protocol_errno_t;

/*
 * Estados de confirmacion de protocolo
 *
*/
typedef enum {
    PROTOCOL_ACK  = 0,
    PROTOCOL_NACK,
    PROTOCOL_ACK_UNKNOWN,
    PROTOCOL_ACK_TIMEOUT
} protocol_ack_t;

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------------------
/*
 * Calcula el CRC-16/CCITT-FALSE.
 *
 * data: bytes sobre los que se calcula el CRC.
 * length: cantidad de bytes.
 *
 * Retorna el CRC de 16 bits.
 */
uint16_t protocol_crc16(
    const uint8_t *data,
    size_t length
);
/* NOTA: para el protocolo CRC-16 se valida sobre los bytes de version hasta el payload */

// ---------------------------------------------------------------------------------------
/*
 * Estructura para representar un frame del protocolo 
 *
*/
typedef struct {
    uint8_t header [PROTOCOL_HEADER_BYTES]; // inicio de trama
    uint8_t version;    // version del protocolo
    uint8_t relevance;  // relevancia del mensage
    uint32_t length;    // largo effectivo del payload
    uint8_t payload[PROTOCOL_PAYLOAD_BYTES]; // payload
    uint16_t crc;       // CRC-16 de la trama
} protocol_frame_t;

/*
 * Cargar propiedades de un frame a un buffer. Inicio de trama, version y CRC
 * son configurados por la funcion. El buffer debe ser del tamaño de un frame.
 * Serializar componentes.
 * 
 * src: puntero a un estructra
 * dst: puntero a un buffer de bytes
 * length: cantidad de bytes del buffer
 *
 * Retorna codigo de error.
*/
protocol_errno_t protocol_set_frame_to_buffer(
    protocol_frame_t* src,
    uint8_t* dst,
    size_t length
);

/*
 * Cargar propiedades a un frame desde un buffer. El buffer debe ser 
 * del tamaño de un frame. Deserializar componentes
 * 
 * dst: puntero a un estructra
 * src: puntero a un buffer de bytes
 * length: cantidad de bytes del buffer
 *
 * Retorna codigo de error
*/
protocol_errno_t protocol_set_buffer_to_frame(
    protocol_frame_t* dst,
    const uint8_t* src,
    size_t length
);

// ---------------------------------------------------------------------------------------
/*
 * Estados de transmision del protocolo
 *
*/
typedef enum {
    PROTOCOL_TX_INIT = 0,
    PROTOCOL_TX_HEADER,
    PROTOCOL_TX_VERSION,
    PROTOCOL_TX_RELEVANCE,
    PROTOCOL_TX_LENGTH,
    PROTOCOL_TX_PAYLOAD,
    PROTOCOL_TX_CRC,
    PROTOCOL_TX_ACK,
    PROTOCOL_TX_DONE
} protocol_tx_state_t;

/*
 * Estructura de metadatos de transmision
 *
*/
typedef struct {
    uint8_t buffer [PROTOCOL_FRAME_BYTES]; // buffer de transmision
    uint8_t ack [PROTOCOL_ACK_BYTES];      // buffer de recepcion de ACK/NACK
    protocol_ack_t ack_state;   // estado de confirmacion
    protocol_tx_state_t state;  // estado de transmision
    bool complete;  // frame completo enviado
    bool valid;     // frame validado
    bool wd_busy;   // TX ocupado con escritura
    bool rd_busy;   // TX ocupado con lectura
    size_t offset;  // offset de TX en lectura del ACK
    size_t count;   // contador de TX para lecturas de ACK
} protocol_tx_t;

/*
 * Inicializar componentes de transmision
 *
*/
protocol_errno_t protocol_tx_init(
    protocol_tx_t* tx,
    serial_packet_t* packet
);

/*
 * Solicitar actualizacion de maquina de estados de transmision. Avanza
 * progreso de transmision.
 * 
 * obj: interfaz serial
 * tx: estructura de transmision
 * packet: paquete serial para comunicacion
 * 
 * Retorna codigo de error.
*/
protocol_errno_t protocol_tx_poll(
    void* obj,
    protocol_tx_t*  tx,
    serial_packet_t* packet
);

// ---------------------------------------------------------------------------------------
/*
 * Estados de recepcion del protocolo
 *
*/
typedef enum {
    PROTOCOL_RX_INIT = 0,
    PROTOCOL_RX_HEADER,
    PROTOCOL_RX_VERSION,
    PROTOCOL_RX_RELEVANCE,
    PROTOCOL_RX_LENGTH,
    PROTOCOL_RX_PAYLOAD,
    PROTOCOL_RX_CRC,
    PROTOCOL_RX_ACK,
    PROTOCOL_RX_DONE
} protocol_rx_state_t;

/*
 * Estructura de metadatos de recepcion
 *
*/
typedef struct {
    uint8_t buffer [PROTOCOL_FRAME_BYTES]; // buffer de recepcion
    uint8_t ack [PROTOCOL_ACK_BYTES];      // bytes de confirmacion
    protocol_ack_t ack_state;   // estado de confirmacion
    protocol_rx_state_t state;  // estado de recepcion
    bool complete;  // frame completo enviado
    bool valid;     // frame validado
    bool wd_busy;   // RX ocupado con escritura
    bool rd_busy;   // RX ocupado con lectura
    size_t offset;  // offset de RX en lectura del encabezado
} protocol_rx_t;

/*
 * Inicializar componentes de recepcion
 *
*/
protocol_errno_t protocol_rx_init(
    protocol_rx_t* rx,
    serial_packet_t* packet
);


/*
 * Solicitar actualizacion de maquina de estados de recepcion. Avanza
 * progreso de recepcion.
 * 
 * obj: interfaz serial
 * rx: estructura de recepcion
 * packet: paquete serial para comunicacion
 * 
 * Retorna codigo de error.
*/
protocol_errno_t protocol_rx_poll(
    void* obj,
    protocol_rx_t*  rx,
    serial_packet_t* packet
);


#ifdef __cplusplus
}
#endif

#endif // COMMON_PROTOCOL_H