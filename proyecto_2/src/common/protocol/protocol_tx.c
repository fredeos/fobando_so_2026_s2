// > Archivo de implementacion <protocol.h>
#include "common/protocol.h"
#include "common/conversion.h"

// + C libraries
#include <string.h>

// =================================================================================================

protocol_errno_t protocol_tx_init(
    protocol_tx_t* tx,
    serial_packet_t* packet
){
    // << Excepcions >>
    if (tx == NULL || packet == NULL)
        return PROTOCOL_INVALID_ARG;
    // << Configuracion de paquete >>
    packet->mode = SERIAL_OUT,
    packet->buffer = NULL,
    packet->length = 0,
    packet->complete = false;
    packet->processed = 0;
    // << Configuracion de TX >>
    tx->complete = false;
    tx->valid = false;
    tx->state = PROTOCOL_TX_INIT;
    tx->ack_state = PROTOCOL_ACK_UNKNOWN;
    tx->rd_busy = false;
    tx->wd_busy = false;
    tx->offset = 0;
    tx->count = 0;
    // << Retorno >>
    return PROTOCOL_OK;
}

// =================================================================================================

protocol_errno_t protocol_tx_poll(
    void* obj,
    protocol_tx_t*  tx,
    serial_packet_t* packet
){
    // << Excepciones >>
    if (tx == NULL || packet == NULL)
        return PROTOCOL_INVALID_ARG;
    protocol_errno_t errno = PROTOCOL_OK;
    // << Lecturas >>
    if (tx->rd_busy){
        size_t rd;
        serial_errno_t rd_errno = serial_read(
            obj,
            packet,
            &rd
        );
        if (rd_errno == SERIAL_DISCONNECT){
            errno = PROTOCOL_SERIAL_EIO;
            serial_restart_packet(packet);
        } else if (rd_errno == SERIAL_FAILURE){
            errno = PROTOCOL_ERROR;
            serial_restart_packet(packet);
        }
    }
    // << Escrituas >>
    if (tx->wd_busy){
        size_t wd;
        serial_errno_t wd_errno = serial_write(
            obj,
            packet,
            &wd
        );
        if (wd_errno == SERIAL_DISCONNECT){
            errno = PROTOCOL_SERIAL_EIO;
            serial_restart_packet(packet);
        } else if (wd_errno == SERIAL_FAILURE){
            errno = PROTOCOL_ERROR;
            serial_restart_packet(packet);
        }
    }
    // << Maquina de estados >>
    uint8_t* buffer = packet->buffer;
    size_t length = packet->length;
    serial_packet_mode_t mode = packet->mode;

    protocol_tx_state_t next = tx->state;
    switch (tx->state){
        case (PROTOCOL_TX_INIT): {
            // [Inicializar TX]
            tx->count = 0;
            tx->offset = 0;
            tx->complete = false;
            tx->valid = false;
            tx->ack_state = PROTOCOL_ACK_UNKNOWN;
            // [Configurar siguiente paquete]
            mode = SERIAL_OUT;
            buffer = tx->buffer + PROTOCOL_HEADER_OFFSET;
            length = PROTOCOL_HEADER_BYTES;
            // [Configurar siguiente estado]
            next = PROTOCOL_TX_HEADER;
            break;
        }

        case (PROTOCOL_TX_HEADER): {
            // [Esperar paquete completo]
            if (packet->complete){
                // [Configurar siguiente paquete]
                mode = SERIAL_OUT;
                buffer = tx->buffer + PROTOCOL_VERSION_OFFSET;
                length = PROTOCOL_VERSION_BYTES;
                // [Configurar siguiente estado]
                next = PROTOCOL_TX_VERSION;
            }
            break;
        }

        case (PROTOCOL_TX_VERSION): {
            // [Esperar paquete completo]
            if (packet->complete){
                // [Configurar siguiente paquete]
                mode = SERIAL_OUT;
                buffer = tx->buffer + PROTOCOL_RELEVANCE_OFFSET;
                length = PROTOCOL_RELEVANCE_BYTES;
                // [Configurar siguiente estado]
                next = PROTOCOL_TX_RELEVANCE;
            }
            break;
        }

        case (PROTOCOL_TX_RELEVANCE): {
            // [Esperar paquete completo]
            if (packet->complete){
                // [Configurar siguiente paquete]
                mode = SERIAL_OUT;
                buffer = tx->buffer + PROTOCOL_LENGTH_OFFSET;
                length = PROTOCOL_LENGTH_BYTES;
                // [Configurar siguiente estado]
                next = PROTOCOL_TX_LENGTH;
            }
            break;
        }

        case (PROTOCOL_TX_LENGTH): {
            // [Esperar paquete completo]
            if (packet->complete){
                // [Configurar siguiente paquete]
                mode = SERIAL_OUT;
                buffer = tx->buffer + PROTOCOL_PAYLOAD_OFFSET;
                length = PROTOCOL_PAYLOAD_BYTES;
                // [Configurar siguiente estado]
                next = PROTOCOL_TX_PAYLOAD;
            }
            break;
        }

        case (PROTOCOL_TX_PAYLOAD): {
            // [Esperar paquete completo]
            if (packet->complete){
                // [Configurar siguiente paquete]
                mode = SERIAL_OUT;
                buffer = tx->buffer + PROTOCOL_CRC_OFFSET;
                length = PROTOCOL_CRC_BYTES;
                // [Configurar siguiente estado]
                next = PROTOCOL_TX_CRC;
            }
            break;
        }

        case (PROTOCOL_TX_CRC): {
            // [Esperar paquete completo]
            if (packet->complete){
                // [Configurar siguiente paquete]
                tx->offset = 0;
                mode = SERIAL_IN;
                buffer = tx->ack;
                length = 1; // byte-to-byte
                // [Configurar siguiente estado]
                next = PROTOCOL_TX_ACK;
            }
            break;
        }

        case (PROTOCOL_TX_ACK): {
            // [Esperar byte leido]
            if (packet->complete && tx->ack_state != PROTOCOL_ACK_TIMEOUT){
                tx->count++;
                // [Verificar byte leido]
                uint8_t byte = tx->ack[tx->offset];
                size_t wh = tx->offset % 2;
                bool ack = false;
                bool nack = false;
                if (wh == 0){
                    ack  = (byte == PROTOCOL_ACK_0);
                    nack = (byte == PROTOCOL_NACK_0);
                } else if (wh == 1){
                    ack  = (byte == PROTOCOL_ACK_1);
                    nack = (byte == PROTOCOL_NACK_1);
                }
                // [Identificar tipo de ACK]
                protocol_ack_t curr_ack = tx->ack_state;
                protocol_ack_t new_ack;
                new_ack = 
                    (ack)  ? PROTOCOL_ACK : 
                    (nack) ? PROTOCOL_NACK : PROTOCOL_ACK_UNKNOWN;

                bool ack_changed = (curr_ack != new_ack);
                bool ack_unknown = (new_ack == PROTOCOL_ACK_UNKNOWN);
                
                tx->ack_state = (ack_changed) ? new_ack : curr_ack;
                if (ack_unknown) tx->offset = 0;
                else tx->offset = (ack_changed) ? 1 : tx->offset+1;
                // [Configurar el siguiente estado]
                bool ack_complete = (tx->offset == PROTOCOL_ACK_BYTES);
                bool ack_timeout  = (tx->count == PROTOCOL_MAX_ATTEMPTS);
                if (ack_complete){
                    next = PROTOCOL_TX_DONE;
                } else if (ack_timeout){
                    tx->ack_state = PROTOCOL_ACK_TIMEOUT;
                    next = PROTOCOL_TX_DONE;
                } else {
                    // [Configurar el siguiente paquete]
                    mode = SERIAL_IN;
                    buffer = tx->ack + tx->offset;
                    length = 1; // byte-to-byte
                    serial_restart_packet(packet); // <- esperar siguiente byte
                }
            }
            break;
        }

        case (PROTOCOL_TX_DONE): {
            // [Actualizar banderas]
            tx->complete = true;
            tx->valid = (tx->ack_state == PROTOCOL_ACK);
            // [Configurar el siguiente estado]
            next = PROTOCOL_TX_INIT;
            break;
        }
    }
    // << Guardar configuraciones>>
    if (errno == PROTOCOL_OK){
        // [Configuracion]
        packet->mode = mode;
        packet->buffer = buffer;
        packet->length = length;
        // [Estado]
        if (next != tx->state){
            serial_errno_t ser_errno = serial_restart_packet(packet);
            tx->state = next;
            if (ser_errno == SERIAL_FAILURE)
                errno = PROTOCOL_ERROR;
        }
    }
    // << Actualizar banderas >>
    tx->rd_busy = (tx->state == PROTOCOL_TX_ACK);
    tx->wd_busy = (
        tx->state != PROTOCOL_TX_INIT &&
        tx->state != PROTOCOL_TX_ACK &&
        tx->state != PROTOCOL_TX_DONE
    );
    // << Retornar >>
    return errno;
}

// =================================================================================================