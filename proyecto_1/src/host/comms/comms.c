// Implementation file of comms.c
#include "comms.h"

#include <stdio.h>
#include <unistd.h>
#include <string.h>

// ===============================================================================================

comms_status_t comms_init_handler(
    int id,
    comms_handler_t* handler,
    const char* dev,
    uint8_t priority,
    unsigned int tick,
    unsigned int tick_limit
){
    // << Exceptions >>
    if (handler == NULL || dev == NULL)
        return COMMS_INVALID_ARG;
    // << Configure handler >>
    handler->dev = dev;
    handler->id  = id;

    handler->status = COMMS_OK;
    handler->transactions = 0;
    
    handler->tick = tick;
    handler->tick_limit = tick_limit;

    handler->online  = false;
    handler->running = false;
    handler->active  = false;

    handler->priority = priority;

    pthread_mutex_init(&handler->mutex, NULL);
    pthread_cond_init(&handler->cond, NULL);
    // << Configure handler messages >>
    handler->recv.content = NULL;
    handler->recv.size = 0;
    handler->recv.priority = 0;
    handler->recv.set = false;
    handler->recv.rdy = false;

    handler->sent.content = NULL;
    handler->sent.size = 0;
    handler->sent.priority = 0;
    handler->sent.set = false;
    handler->sent.rdy = false;
    // << Return >>
    return COMMS_OK;
}

// ===============================================================================================

comms_status_t comms_setup_handler(
    comms_handler_t* handler
){
    // << Exceptions >>
    if (handler == NULL)
        return COMMS_INVALID_ARG;

    // << Start thread >>
    pthread_t handler_thread;
    int code = pthread_create(
        &handler_thread,
        NULL,
        comms_handler,
        handler
    );
    if (code != 0) return COMMS_ERROR;
    pthread_detach(handler_thread);
    // << Return >>
    return COMMS_OK;
}

comms_status_t comms_stop_handler(
    comms_handler_t* handler
){  
    pthread_mutex_lock(&handler->mutex);   // lock
    handler->active = false;
    pthread_mutex_unlock(&handler->mutex); // unlock
    return COMMS_OK;
}

// ===============================================================================================

comms_status_t comms_request_send(
    comms_handler_t* handler,
    const char* message,
    uint8_t priority
){
    if (handler == NULL || message == NULL || handler->sent.content == NULL)
        return COMMS_INVALID_ARG;

    uint32_t size = strlen(message) + 1;
    if (size > COMMS_MESSAGE_CAPACITY || size > PROTOCOL_PAYLOAD_BYTES)
        return COMMS_INVALID_ARG;

    /* El buffer compartido con el hilo se actualiza bajo el mismo mutex. */
    pthread_mutex_lock(&handler->mutex);   // lock
    if (handler->sent.set) {
        pthread_mutex_unlock(&handler->mutex);
        return COMMS_BUSY;
    }
    handler->sent.priority = priority;
    handler->sent.size = size;
    memmove(
        handler->sent.content,
        message,
        size
    );

    handler->sent.set = true;
    pthread_mutex_unlock(&handler->mutex); // unlock

    return COMMS_OK;
}

comms_status_t comms_acquire_send(
    comms_handler_t* handler
){
    pthread_mutex_lock(&handler->mutex);   // lock
    handler->sent.rdy = false;
    pthread_mutex_unlock(&handler->mutex); // unlock
    
    return COMMS_OK;
}

comms_status_t comms_request_receive(
    comms_handler_t* handler
){
    if (handler->recv.set)
        return COMMS_BUSY;
    
    pthread_mutex_lock(&handler->mutex);   // lock
    handler->recv.set = true;
    pthread_mutex_unlock(&handler->mutex);   // unlock

    return COMMS_OK;
}

comms_status_t comms_acquire_receive(
    comms_handler_t* handler
){
    pthread_mutex_lock(&handler->mutex);   // lock
    handler->recv.rdy = false;
    pthread_mutex_unlock(&handler->mutex);   // unlock

    return COMMS_OK;
}

// ===============================================================================================

void* comms_handler(void* arg){
    bool setup_fail = false;
    // << Cast the argument >>
    comms_handler_t* hd = (comms_handler_t*) arg;
    pthread_mutex_lock(&hd->mutex);   // lock
    // << Configure serial interface >>
    posix_serial_config_t ser_conf = {
        .device = hd->dev,
        .baudrate = SERIAL_BAUDRATE
    };

    serial_errno_t ser_errno = serial_configure(
        &hd->fd,
        &ser_conf
    );

    if (!setup_fail) setup_fail = (ser_errno != SERIAL_OK);
    // << Configure protocol interface >>
    protocol_frame_t tx_frame;
    protocol_tx_t tx;
    serial_packet_t tx_packet;
    protocol_errno_t tx_setup_errno = protocol_tx_init(&tx, &tx_packet);

    // [RX:input]
    protocol_frame_t rx_frame;
    protocol_rx_t rx;
    serial_packet_t rx_packet;
    protocol_errno_t rx_setup_errno = protocol_rx_init(&rx, &rx_packet);

    if (!setup_fail) setup_fail = (tx_setup_errno != PROTOCOL_OK || rx_setup_errno != PROTOCOL_OK);
    // << Default values >>
    const char* default_msg = "PING";
    uint32_t default_size = strlen(default_msg) + 1;
    uint8_t default_priority = 0;
    bool default_set = false;

    // << Thread variables >>
    comms_status_t status = COMMS_ALIVE;

    int stage = 0;        // protocol stage transitions; TX <-> RX
    int transactions = 0; // transactions done

    bool sent_pending = false;
    // << Procedure loop >>
    if (setup_fail) {
        printf("Failed to connect to device %s [%d]!\n", hd->dev, hd->id);
    } else { 
        hd->running = true;
        hd->active  = true;
        printf("Connected to device %s [%d] succesfully!\n", hd->dev, hd->id);
    }
    pthread_mutex_unlock(&hd->mutex); // unlock
    while (!setup_fail){
        // [Transactions check]
        bool timeout = false;

        // [Synchronization barrier]
        pthread_mutex_lock(&hd->mutex);   // lock
        hd->online = !timeout;
        if (timeout) status = COMMS_DEAD;

        if(!hd->active && stage == 0){
            hd->status = COMMS_END;
            hd->online = false;
            pthread_mutex_unlock(&hd->mutex); // unlock
            break;
        }

        hd->status = status;

        bool recv_set = hd->recv.set;
        bool recv_rdy = hd->recv.rdy;

        bool sent_set = hd->sent.set;
        bool sent_rdy = hd->sent.rdy;
        pthread_mutex_unlock(&hd->mutex); // unlock
        status = COMMS_ALIVE;
        // [Message configuration]
        if (stage == 0){
            const char* msg = NULL;
            bool msg_set = false;
            uint32_t size = 0;
            uint8_t priority = 0;
            // (Determine message)
            if (sent_set && !sent_rdy){ // set a custom message
                pthread_mutex_lock(&hd->mutex);   // lock
                msg = hd->sent.content;
                size = hd->sent.size;
                priority = hd->sent.priority;
                pthread_mutex_unlock(&hd->mutex); // unlock

                sent_pending = true;
                default_set = false;
                msg_set = true;
            } else if (!default_set){ // set a default message
                msg = default_msg;
                size = default_size;
                priority = default_priority;

                default_set = true;
                msg_set = true;
            }
            // (Set message to frame)
            if (msg_set){
                tx_frame.relevance = priority;
                tx_frame.length = size;
                memcpy(
                    tx_frame.payload,
                    msg,
                    size
                );

                protocol_errno_t tx_frame_errno = protocol_set_frame_to_buffer(
                    &tx_frame,
                    tx.buffer,
                    sizeof(tx.buffer)
                );

                if (tx_frame_errno == PROTOCOL_ERROR) status = COMMS_ERROR;
            }
            // [Move to TX]
            stage = 1;
        }
        // [TX polling]
        if (stage == 1){
            // (Poll TX state machine)
            protocol_errno_t tx_errno = protocol_tx_poll(
                &hd->fd,
                &tx,
                &tx_packet
            );
            // (Check errors)
            if (tx_errno != PROTOCOL_OK) status = COMMS_TX_ERROR;
            else status = COMMS_TX_BUSY;
            // (Check completion)
            if (tx.complete && tx.valid){
                if (sent_pending) {
                    pthread_mutex_lock(&hd->mutex);   // lock
                    hd->sent.set = false;
                    hd->sent.rdy = true;
                    pthread_mutex_unlock(&hd->mutex); // unlock
                    sent_pending = false;
                }
                // [Move to RX]
                stage = 2;
            }
        }
        // [RX polling]
        if (stage == 2){
            // (Poll RX state machine)
            protocol_errno_t rx_errno = protocol_rx_poll(
                &hd->fd,
                &rx,
                &rx_packet
            );
            // (Check errors)
            if (rx_errno != PROTOCOL_OK) status = COMMS_RX_ERROR;
            else status = COMMS_RX_BUSY;
            // (Check completion)
            if (rx.complete){
                if (rx.valid){ // [Extract message]
                    protocol_errno_t rx_frame_errno = protocol_set_buffer_to_frame(
                        &rx_frame,
                        rx.buffer,
                        sizeof(rx.buffer)
                    );
                    if (rx_frame_errno == PROTOCOL_OK){
                        // (Copy contents)
                        pthread_mutex_lock(&hd->mutex);   // lock
                        bool msg_save = false;
                        if (recv_set && !recv_rdy){ // force message save
                            // + Save frame contents
                            msg_save = true;
                            // + Reset flags
                            hd->recv.rdy = true;
                            hd->recv.set = false;
                        } else if (rx_frame.relevance >= hd->priority &&
                                   (!hd->recv.rdy ||
                                    (hd->recv.content != NULL &&
                                     rx_frame.length > 0u &&
                                     rx_frame.payload[0] == '{' &&
                                     (hd->recv.size == 0u ||
                                      hd->recv.content[0] != '{')))){ // prefer app JSON over heartbeat text
                            // + Save frame contents
                            msg_save = true;
                            // + Reset flags
                            hd->recv.rdy = true;
                        }
                        if (msg_save && hd->recv.content != NULL &&
                            rx_frame.length <= MESSAGE_MAX_BYTES){
                            // + Copy frame contents
                            hd->recv.size = rx_frame.length;
                            hd->recv.priority = rx_frame.relevance;
                            memcpy(
                                hd->recv.content,
                                rx_frame.payload,
                                rx_frame.length
                            );
                            hd->recv.content[rx_frame.length] = '\0';
                        }
                        hd->transactions++;
                        pthread_mutex_unlock(&hd->mutex); // unlock
                        // (Count transactions)
                        transactions++;
                    }
                }
                // [Move to message change]
                stage = 0;
            }
        }
    }
    pthread_mutex_lock(&hd->mutex);
    hd->running = false;
    hd->online = false;
    pthread_mutex_unlock(&hd->mutex);
    if (!setup_fail) {
        printf("Closing file descriptor for %s device(%d)\n", hd->dev, hd->id);
        close(hd->fd);
    }
    // << Return >>
    return NULL;
}