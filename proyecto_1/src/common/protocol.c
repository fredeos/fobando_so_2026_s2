// > Implementacion de protocol.h
#include "common/protocol.h"
#include "common/conversion.h"

// =================================================================================================
/*
 * Implementación del CRC-16/CCITT-FALSE.
 *
 * Polinomio: 0x1021
 * Valor inicial: 0xFFFF
 * XOR final: 0x0000
 * Sin reflexión de bits.
 */
uint16_t protocol_crc16(
    const uint8_t *data,
    size_t length
){
    uint16_t crc = 0xFFFFu;

    // Procesar cada byte
    for (size_t i = 0; i < length; ++i) {

        crc ^= (uint16_t)(data[i] << 8);

        // Procesar los 8 bits del byte
        for (unsigned bit = 0; bit < 8; ++bit) {

            if ((crc & 0x8000u) != 0u) {

                crc = (uint16_t)(
                    (crc << 1) ^ 0x1021u
                );

            } else {

                crc = (uint16_t)(crc << 1);

            }
        }
    }

    return crc;
}

// =================================================================================================

protocol_errno_t protocol_set_frame_to_buffer(
    protocol_frame_t* src,
    uint8_t* dst,
    size_t length
){
    // << Identificar excepcions >>
    if (src == NULL || dst == NULL)
        return PROTOCOL_INVALID_ARG;
    if (length != PROTOCOL_FRAME_BYTES)
        return PROTOCOL_INVALID_SIZE;
    if (src->length > PROTOCOL_PAYLOAD_BYTES)
        return PROTOCOL_OUT_RANGE;
    // << Cargar componentes >>
    // 1. Cargar encabezado
    for (size_t i = 0; i < PROTOCOL_HEADER_BYTES; i++){
        size_t mg = i % 2;
        if (mg == 0) dst[i] = PROTOCOL_MAGIC_0;
        if (mg == 1) dst[i] = PROTOCOL_MAGIC_1;
    }
    memcpy(
        src->header,
        dst,
        PROTOCOL_HEADER_BYTES
    );
    // 2. Cargar version
    dst[PROTOCOL_VERSION_OFFSET] = PROTOCOL_VERSION;
    // 3. Cargar relevancia
    dst[PROTOCOL_RELEVANCE_OFFSET] = src->relevance;
    // 4. Cargar largo
    write_u32_be(
        dst + PROTOCOL_LENGTH_OFFSET,
        src->length
    );
    // 5. Cargar payload
    memcpy(
        dst + PROTOCOL_PAYLOAD_OFFSET,
        src->payload,
        PROTOCOL_PAYLOAD_BYTES
    );
    // 6. Generar CRC-16
    uint16_t crc = protocol_crc16(
        dst + PROTOCOL_VERSION_OFFSET,
        PROTOCOL_CRC_OFFSET - PROTOCOL_VERSION_OFFSET
    );
    src->crc = crc;
    write_u16_be(
        dst + PROTOCOL_CRC_OFFSET,
        crc
    );
    // << Retornar >>
    return PROTOCOL_OK;
}

protocol_errno_t protocol_set_buffer_to_frame(
    protocol_frame_t* dst,
    const uint8_t* src,
    size_t length
){
    // << Identificar excepcions >>
    if (src == NULL || dst == NULL)
        return PROTOCOL_INVALID_ARG;
    if (length != PROTOCOL_FRAME_BYTES)
        return PROTOCOL_INVALID_SIZE;
    // << Cargar componentes >>
    // 1. Cargar encabezado
    memcpy(
        dst->header,
        src,
        PROTOCOL_HEADER_BYTES
    );
    // 2. Cargar version
    dst->version = src[PROTOCOL_VERSION_OFFSET];
    // 3. Cargar relevancia
    dst->relevance = src[PROTOCOL_RELEVANCE_OFFSET];
    // 4. Cargar largo
    dst->length = read_u32_be(
        src + PROTOCOL_LENGTH_OFFSET
    );
    // 5. Cargar payload
    memcpy(
        dst->payload,
        src + PROTOCOL_PAYLOAD_OFFSET,
        PROTOCOL_PAYLOAD_BYTES
    );
    // 6. Cargar el CRC-16
    dst->crc = read_u16_be(
        src + PROTOCOL_CRC_OFFSET
    );
    // << Retornar >>
    return PROTOCOL_OK;
}

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
    // << Retorno >>
    return PROTOCOL_OK;
}

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