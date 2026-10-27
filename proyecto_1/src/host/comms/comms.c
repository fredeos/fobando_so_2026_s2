// Implementation file of comms.c
#include "comms.h"

#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include "./../serial/posix.h"
#include "./../timing/timing.h"
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
    
    handler->tick = tick;
    handler->tick_limit = tick_limit;

    handler->online  = false;
    handler->running = false;
    handler->active  = false;
    handler->finished = true;

    handler->priority = priority;

    pthread_mutex_init(&handler->mutex, NULL);
    pthread_cond_init(&handler->cond, NULL);
    // << Configure handler messages >>
    handler->recv.content = NULL;
    handler->recv.size = 0;
    handler->recv.priority = 0;
    handler->recv.set = false;
    handler->recv.rdy = false;
    handler->receive_head = 0u;
    handler->receive_tail = 0u;
    handler->receive_count = 0u;
    handler->receive_overflow_pending = false;
    handler->heartbeat_count = 0u;

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
    pthread_mutex_lock(&handler->mutex);
    handler->active = true;
    handler->finished = false;
    pthread_mutex_unlock(&handler->mutex);
    pthread_t handler_thread;
    int code = pthread_create(
        &handler_thread,
        NULL,
        comms_handler,
        handler
    );
    if (code != 0) {
        pthread_mutex_lock(&handler->mutex);
        handler->active = false;
        handler->finished = true;
        pthread_mutex_unlock(&handler->mutex);
        return COMMS_ERROR;
    }
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
    if (handler == NULL)
        return COMMS_INVALID_ARG;
    pthread_mutex_lock(&handler->mutex);
    if (handler->recv.set) {
        pthread_mutex_unlock(&handler->mutex);
        return COMMS_BUSY;
    }
    handler->recv.set = true;
    pthread_mutex_unlock(&handler->mutex);

    return COMMS_OK;
}

comms_status_t comms_acquire_receive(
    comms_handler_t* handler
){
    if (handler == NULL)
        return COMMS_INVALID_ARG;
    pthread_mutex_lock(&handler->mutex);   // lock
    handler->recv.rdy = handler->receive_count > 0u;
    pthread_mutex_unlock(&handler->mutex);   // unlock

    return COMMS_OK;
}

comms_status_t comms_receive_next(
    comms_handler_t* handler,
    char* buffer,
    size_t capacity,
    size_t* message_size
)
{
    if (handler == NULL || buffer == NULL || message_size == NULL)
        return COMMS_INVALID_ARG;

    pthread_mutex_lock(&handler->mutex);
    if (handler->receive_overflow_pending) {
        handler->receive_overflow_pending = false;
        pthread_mutex_unlock(&handler->mutex);
        return COMMS_RX_ERROR;
    }
    if (handler->receive_count == 0u) {
        pthread_mutex_unlock(&handler->mutex);
        return COMMS_BUSY;
    }

    const comms_received_message_t* message =
        &handler->receive_queue[handler->receive_head];
    if (capacity <= message->size) {
        pthread_mutex_unlock(&handler->mutex);
        return COMMS_INVALID_ARG;
    }

    memcpy(buffer, message->content, message->size);
    buffer[message->size] = '\0';
    *message_size = message->size;
    handler->receive_head =
        (handler->receive_head + 1u) % COMMS_RECEIVE_QUEUE_CAPACITY;
    --handler->receive_count;
    handler->recv.rdy = handler->receive_count > 0u;
    pthread_mutex_unlock(&handler->mutex);
    return COMMS_OK;
}

comms_status_t comms_receive_enqueue(
    comms_handler_t* handler,
    const char* message,
    size_t message_size,
    uint8_t priority
)
{
    if (handler == NULL || message == NULL ||
        message_size == 0u || message_size >= COMMS_MESSAGE_CAPACITY)
        return COMMS_INVALID_ARG;

    pthread_mutex_lock(&handler->mutex);
    bool force_receive = handler->recv.set && !handler->recv.rdy;
    if (!force_receive && priority < handler->priority) {
        pthread_mutex_unlock(&handler->mutex);
        return COMMS_BUSY;
    }
    if (message_size >= 4u && memcmp(message, "PONG", 4u) == 0) {
        ++handler->heartbeat_count;
        pthread_mutex_unlock(&handler->mutex);
        return COMMS_OK;
    }

    if (handler->receive_count == COMMS_RECEIVE_QUEUE_CAPACITY) {
        handler->receive_overflow_pending = true;
        pthread_mutex_unlock(&handler->mutex);
        return COMMS_RX_ERROR;
    }

    comms_received_message_t* slot =
        &handler->receive_queue[handler->receive_tail];
    memcpy(slot->content, message, message_size);
    slot->content[message_size] = '\0';
    slot->size = (uint32_t)message_size;
    slot->priority = priority;
    handler->receive_tail =
        (handler->receive_tail + 1u) % COMMS_RECEIVE_QUEUE_CAPACITY;
    ++handler->receive_count;
    handler->recv.rdy = true;
    if (force_receive)
        handler->recv.set = false;
    pthread_mutex_unlock(&handler->mutex);
    return COMMS_OK;
}

uint64_t comms_heartbeat_count(comms_handler_t* handler)
{
    if (handler == NULL)
        return 0u;
    pthread_mutex_lock(&handler->mutex);
    uint64_t count = handler->heartbeat_count;
    pthread_mutex_unlock(&handler->mutex);
    return count;
}

bool comms_reports_drained_since(
    comms_handler_t* handler,
    uint64_t heartbeat_baseline
)
{
    if (handler == NULL)
        return false;
    pthread_mutex_lock(&handler->mutex);
    bool drained = handler->receive_count == 0u &&
        handler->heartbeat_count != heartbeat_baseline;
    pthread_mutex_unlock(&handler->mutex);
    return drained;
}

// ===============================================================================================

void* comms_handler(void* arg){
    // << Cast the argument >>
    comms_handler_t* hd = (comms_handler_t*) arg;
    // << Configure serial interface >>
    posix_serial_config_t ser_conf = {
        .device = hd->dev,
        .baudrate = SERIAL_BAUDRATE
    };
    // << Configure protocol interface >>
    // [TX:output]
    protocol_frame_t tx_frame;
    protocol_tx_t tx;
    serial_packet_t tx_packet;
    
    // [RX:input]
    protocol_frame_t rx_frame;
    protocol_rx_t rx;
    serial_packet_t rx_packet;

    // << Default values >>
    const char* default_msg = "PING";
    uint32_t default_size = strlen(default_msg) + 1;
    uint8_t default_priority = 0;
    bool default_set = false;

    // << Thread variables >>
    comms_status_t status = COMMS_ALIVE;
    uint64_t last = get_time_ms();
    uint64_t ticks = 0;

    int stage = 0;        // protocol stage transitions; MSG <-> TX <-> RX
    int transactions = 0; // transactions done

    bool sent_pending  = false; // custom message is pending to be sent
    bool serial_refresh = true; // flag for interface refresh
    // << Procedure loop >>
    pthread_mutex_lock(&hd->mutex);   // lock
    hd->online  = false;
    hd->running = false;
    hd->finished = false;
    pthread_mutex_unlock(&hd->mutex); // unlock
    while (1){
        // [Refresh check]
        /* The refresh check allow initializing protocol and serial interface components for
         * connection, or reconnection in case device disconnection.
         * This also handles transaction restarts to allow correct flow; this works by
         * setting back the state to 0 and re-setting messages.
        */
        if (serial_refresh){ // check for refresh signal
            serial_errno_t ser_errno = serial_configure(
                &hd->fd,
                &ser_conf
            ); // <- try to setup the serial interface and flush it.
            if (ser_errno == SERIAL_OK){
                printf("%s: Device(%d) connected!\n", hd->dev, hd->id);
                protocol_tx_init(&tx, &tx_packet); // <- this will reset the TX state machine
                protocol_rx_init(&rx, &rx_packet); // <- this will reset the RX state machine
                serial_refresh = false;
                stage = 0;
                transactions = 0;
            }
        }
        // [Transactions check]
        /* Transactions check allows verifying if the handler has received transactions
         * in the defined number of ticks. This allows checking the responsiveness of
         * the connected device.
        */
        uint64_t now = get_time_ms();
        bool on_tick = (now - last >= hd->tick);
        if (on_tick){
            ticks++;
            last += hd->tick;
        }

        bool minimum = (transactions > 0);
        bool full_ticks = (ticks >= hd->tick_limit);
        if (full_ticks) {
            ticks = 0;
            transactions = 0;
        }

        bool timeout = (full_ticks && !minimum && !serial_refresh);
        if (timeout) {
            printf("%s: Device(%d) disconnected by timeout!\n", hd->dev, hd->id);
            if (hd->fd >= 0) {
                close(hd->fd);
                hd->fd = -1;
            }
            stage = 0;
            serial_refresh = true;
            status = COMMS_TX_ERROR;
        }
        // [Synchronization barrier]
        /* The barrier allows the handler synchronizing with main thread to check
         * it's own status flags for events.
        */
        pthread_mutex_lock(&hd->mutex);   // lock
        // (Set thread as running)
        hd->running = true;
        // (Set device online)
        if (serial_refresh) hd->online = false;
        else hd->online = !timeout;

        if (!hd->online) status = COMMS_DEAD;
        // (Thread killed)
        if(!hd->active && stage == 0){
            // + Update status and flags
            hd->status = COMMS_END;
            hd->online = false;
            hd->running = false;
            hd->finished = true;
            pthread_mutex_unlock(&hd->mutex); // unlock
            // + Close file descriptor
            if (!serial_refresh && hd->fd >= 0){
                printf("Closing file descriptor for device(%d)\n", hd->id);
                close(hd->fd);
                hd->fd = -1;
            }
            break;
        }
        // (Update status)
        hd->status = status;
        // (Extract requests)
        bool sent_set = hd->sent.set;
        bool sent_rdy = hd->sent.rdy;
        pthread_mutex_unlock(&hd->mutex); // unlock

        status = COMMS_ALIVE;
        // [Refresh skip]
        /* If the serial interface has no connection skips the whole prtocol polling */
        if (serial_refresh) continue;

        // [Message configuration]
        /* Setup a message for TX (stage 1). Checks if there are requests for custom messages
         * and if it's possible to attend them. Otherwise, sets a default message; default
         * message only has to be set once every time a custom message overrides it.
        */
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
        /* TX polling allows making the state machine progress and send each component
         * of the defined frame. If the protocol throws an error from the serial interface
         * it forces a serial refresh to check if the current device is still connected.
         * Otherwise, if it still retains connection but it's waiting for an ACK; it eventually
         * reaches a timeout which allows resending the whole frame again; however this could
         * lead to a deadlock if RX on other device never reaches the ACK state. The ACK state
         * allows to check the current frame as valid if the other device received it.
        */
        if (stage == 1){
            // (Poll TX state machine)
            protocol_errno_t tx_errno = protocol_tx_poll(
                &hd->fd,
                &tx,
                &tx_packet
            );
            // (Check errors)
            if (tx_errno == PROTOCOL_SERIAL_EIO) {
                printf("%s: Device(%d) disconnected by error!\n", hd->dev, hd->id);
                if (hd->fd >= 0) {
                    close(hd->fd);
                    hd->fd = -1;
                }
                stage = 0;
                serial_refresh = true;
                status = COMMS_TX_ERROR;
            } else status = COMMS_TX_BUSY;
            // (Check completion)
            if (tx.complete && tx.valid){
                if (sent_pending) {
                    pthread_mutex_lock(&hd->mutex);   // lock
                    hd->sent.set = false;
                    hd->sent.rdy = true;
                    pthread_mutex_unlock(&hd->mutex); // unlock
                    sent_pending = false;
                }
                // [Count transactions]
                transactions++;
                // [Move to RX]
                stage = 2;
            }
        }
        // [RX polling]
        /* RX polling allows making the state machine progress and receive components of
         * a frame from an connected device. If the protoco, throws and error from the serial
         * interface it forces a serial refresh to check if the currect device is still connected.
         * The RX must wait for a valid header to start, meaning if TX from the other device never
         * sends or sends an incorrect header the RX on this end will deadlock. However, the received
         * frame doesn't have to be valid, since after receiving the complete frame it will move to stage 0
        */
        if (stage == 2){
            // (Poll RX state machine)
            protocol_errno_t rx_errno = protocol_rx_poll(
                &hd->fd,
                &rx,
                &rx_packet
            );
            // (Check errors)
            if (rx_errno == PROTOCOL_SERIAL_EIO) {
                printf("%s: Device(%d) disconnected by error!\n", hd->dev, hd->id);
                stage = 0;
                serial_refresh = true;
                status = COMMS_RX_ERROR;
                if (hd->fd >= 0) {
                    close(hd->fd);
                    hd->fd = -1;
                }
            } else status = COMMS_RX_BUSY;
            // (Check completion)
            if (rx.complete){
                if (rx.valid){ // [Extract message]
                    protocol_errno_t rx_frame_errno = protocol_set_buffer_to_frame(
                        &rx_frame,
                        rx.buffer,
                        sizeof(rx.buffer)
                    );
                    if (rx_frame_errno == PROTOCOL_OK){
                        comms_status_t queued = comms_receive_enqueue(
                            hd, (const char*)rx_frame.payload, rx_frame.length,
                            rx_frame.relevance
                        );
                        if (queued == COMMS_RX_ERROR) {
                            fprintf(stderr,
                                "%s: receive queue full; incoming message dropped.\n",
                                hd->dev);
                        }
                    }
                }
                // [Count transactions]
                transactions++;
                // [Move to message change]
                stage = 0;
            }
        }
    }
    pthread_mutex_lock(&hd->mutex);
    hd->running = false;
    hd->online = false;
    pthread_mutex_unlock(&hd->mutex);
    if (hd->fd >= 0) {
        printf("Closing file descriptor for %s device(%d)\n", hd->dev, hd->id);
        close(hd->fd);
        hd->fd = -1;
    }
    // << Return >>
    return NULL;
}