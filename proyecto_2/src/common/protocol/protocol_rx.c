// > Archivo de implementacion <protocol.h>
#include "common/protocol.h"
#include "common/conversion.h"

// + C libraries
#include <string.h>

// =================================================================================================

protocol_errno_t protocol_rx_init(
    protocol_rx_t* rx,
    serial_packet_t* packet
){
    // << Excepcions >>
    if (rx == NULL || packet == NULL)
        return PROTOCOL_INVALID_ARG;
    // << Configuracion de paquete >>
    packet->mode = SERIAL_IN,
    packet->buffer = NULL,
    packet->length = 0,
    packet->complete = false;
    packet->processed = 0;
    // << Configuracion de TX >>
    rx->complete = false;
    rx->valid = false;
    rx->state = PROTOCOL_RX_INIT;
    rx->ack_state = PROTOCOL_ACK_UNKNOWN;
    rx->rd_busy = false;
    rx->wd_busy = false;
    rx->offset = 0;
    memset(rx->buffer, 0, sizeof(rx->buffer));
    // << Retorno >>
    return PROTOCOL_OK;
}

// =================================================================================================

protocol_errno_t protocol_rx_poll(
    void* obj,
    protocol_rx_t*  rx,
    serial_packet_t* packet
){
    // << Excepciones >>
    if (rx == NULL || packet == NULL)
        return PROTOCOL_INVALID_ARG;
    protocol_errno_t errno = PROTOCOL_OK;
    // << Lecturas >>
    if (rx->rd_busy){
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
    if (rx->wd_busy){
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

    protocol_rx_state_t next = rx->state;
    switch (rx->state){
        case (PROTOCOL_RX_INIT): {
            // [Inicializar RX]
            rx->complete = false;
            rx->valid = false;
            rx->ack_state = PROTOCOL_ACK_UNKNOWN;
            // [Configurar siguiente paquete]
            rx->offset = PROTOCOL_HEADER_OFFSET;
            mode = SERIAL_IN;
            buffer = rx->buffer + PROTOCOL_HEADER_OFFSET;
            length = 1; // byte-to-byte
            // [Configurar siguiente estado]
            next = PROTOCOL_RX_HEADER;
            break;
        }

        case (PROTOCOL_RX_HEADER): {
            // [Esperar byte leido]
            if (packet->complete){
                // [Verificar byte leido]
                uint8_t byte = rx->buffer[rx->offset];
                size_t wh = rx->offset % 2;
                uint8_t expected = (wh == 0) ? PROTOCOL_MAGIC_0 : PROTOCOL_MAGIC_1;

                bool is_expected = (byte == expected);
                bool is_init = (byte == PROTOCOL_MAGIC_0);
                // [Identificar inicio de trama]
                if (is_expected){
                    rx->offset++;
                } else if (rx->offset > PROTOCOL_HEADER_OFFSET && is_init){
                    rx->buffer[PROTOCOL_HEADER_OFFSET] = byte;
                    rx->offset = PROTOCOL_HEADER_OFFSET + 1;
                } else {
                    rx->offset = PROTOCOL_HEADER_OFFSET;
                }
                // [Deteminar paquete completado]
                bool header_complete = (rx->offset == PROTOCOL_VERSION_OFFSET);
                if (header_complete){
                    // [Configurar el siguiente paquete]
                    mode = SERIAL_IN;
                    buffer = rx->buffer + PROTOCOL_VERSION_OFFSET;
                    length = PROTOCOL_VERSION_BYTES;
                    // [Configurar siguiente estado]
                    next = PROTOCOL_RX_VERSION;
                } else {
                    // [Configurar el siguiente paquete]
                    mode = SERIAL_IN;
                    buffer = rx->buffer + rx->offset;
                    length = 1; //  byte-to-byte
                    serial_restart_packet(packet); // <- esperar siguiente byte
                }
            }
            break;
        }

        case (PROTOCOL_RX_VERSION): {
            // [Esperar paquete completo]
            if (packet->complete){
                // [Configurar siguiente paquete]
                mode = SERIAL_IN;
                buffer = rx->buffer + PROTOCOL_RELEVANCE_OFFSET;
                length = PROTOCOL_RELEVANCE_BYTES;
                // [Configurar siguiente estado]
                next = PROTOCOL_RX_RELEVANCE;
            }
            break;
        }

        case (PROTOCOL_RX_RELEVANCE): {
            // [Esperar paquete completo]
            if (packet->complete){
                // [Configurar siguiente paquete]
                mode = SERIAL_IN;
                buffer = rx->buffer + PROTOCOL_LENGTH_OFFSET;
                length = PROTOCOL_LENGTH_BYTES;
                // [Configurar siguiente estado]
                next = PROTOCOL_RX_LENGTH;
            }
            break;
        }

        case (PROTOCOL_RX_LENGTH): {
            // [Esperar paquete completo]
            if (packet->complete){
                // [Configurar siguiente paquete]
                mode = SERIAL_IN;
                buffer = rx->buffer + PROTOCOL_PAYLOAD_OFFSET;
                length = PROTOCOL_PAYLOAD_BYTES;
                // [Configurar siguiente estado]
                next = PROTOCOL_RX_PAYLOAD;
            }
            break;
        }

        case (PROTOCOL_RX_PAYLOAD): {
            // [Esperar paquete completo]
            if (packet->complete){
                // [Configurar siguiente paquete]
                mode = SERIAL_IN;
                buffer = rx->buffer + PROTOCOL_CRC_OFFSET;
                length = PROTOCOL_CRC_BYTES;
                // [Configurar siguiente estado]
                next = PROTOCOL_RX_CRC;
            }
            break;
        }

        case (PROTOCOL_RX_CRC): {
            // [Esperar paquete completo]
            if (packet->complete){
                // [Extraer CRC-16]
                uint16_t recv_crc = read_u16_be(
                    rx->buffer + PROTOCOL_CRC_OFFSET
                );
                // [Generar CRC-16]
                uint16_t frame_crc = protocol_crc16(
                    rx->buffer + PROTOCOL_VERSION_OFFSET,
                    PROTOCOL_CRC_OFFSET - PROTOCOL_VERSION_OFFSET
                );
                // [Validar CRC-16]
                bool valid_frame = (recv_crc == frame_crc);
                for (size_t i = 0; i < PROTOCOL_ACK_BYTES; i++){
                    size_t wh = i % 2;
                    if (wh == 0) 
                        rx->ack[i] = (valid_frame) ? PROTOCOL_ACK_0 : PROTOCOL_NACK_0;
                    else if (wh == 1) 
                        rx->ack[i] = (valid_frame) ? PROTOCOL_ACK_1 : PROTOCOL_NACK_1;
                }
                rx->valid = valid_frame;
                rx->ack_state = (valid_frame) ? PROTOCOL_ACK : PROTOCOL_NACK;
                // [Configurar siguiente paquete]
                mode = SERIAL_OUT;
                buffer = rx->ack;
                length = PROTOCOL_ACK_BYTES;
                // [Configurar siguiente estado]
                next = PROTOCOL_RX_ACK;
            }
            break;
        }

        case (PROTOCOL_RX_ACK): {
            // [Esperar paquete completo]
            if (packet->complete){
                // [Configurar siguiente estado]
                next = PROTOCOL_RX_DONE;
            }
            break;
        }

        case (PROTOCOL_RX_DONE): {
            // [Actualizar banderas]
            rx->complete = true;
            // [Configurar siguiente estado]
            next = PROTOCOL_RX_INIT;
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
        if (next != rx->state){
            serial_errno_t ser_errno = serial_restart_packet(packet);
            rx->state = next;
            if (ser_errno == SERIAL_FAILURE)
                errno = PROTOCOL_ERROR;
        }
    }
    // << Actualizar banderas >>
    rx->wd_busy = (rx->state == PROTOCOL_RX_ACK);
    rx->rd_busy = (
        rx->state != PROTOCOL_RX_INIT &&
        rx->state != PROTOCOL_RX_ACK &&
        rx->state != PROTOCOL_RX_DONE
    );
    // << Retornar >>
    return errno;
}

// =================================================================================================