#ifndef HOST_COMMS_H
#define HOST_COMMS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <pthread.h>

#include "common/protocol.h"
#include "common/message_parser.h"
#include "serial/serial.h"
#include "./../serial/posix.h"

#define COMMS_MESSAGE_CAPACITY (MESSAGE_MAX_BYTES + 1u)

/*
 * Communications status codes
 *
*/
typedef enum {
    // General status
    COMMS_OK = 0,
    COMMS_ERROR = -1,
    COMMS_END = 1,
    COMMS_BUSY = -2,
    // Parameter errors
    COMMS_INVALID_ARG = -3,
    // Connection status
    COMMS_ALIVE = 100,
    COMMS_DEAD  = -100,
    // Communication status
    COMMS_TX_BUSY = 15,
    COMMS_TX_ERROR = -10,
    COMMS_RX_BUSY = 30,
    COMMS_RX_ERROR = -20
} comms_status_t;

// ==================================================================================
#ifdef __cplusplus
extern "C" {
#endif

// ----------------------------------------------------------------------------------
/*
 * Structure for a communication message for handler
 *
*/
typedef struct {
    char* content;   // message content
    uint32_t size;   // message size; length of string
    uint8_t priority;// message priority level
    bool set;   // flag to trigger transfer
    bool rdy;   // flag to signal transfer complete
} comms_message_t;

/*
 * Handler structure for communications via serial and protocol interface
 * 
*/
typedef struct {
    // << Information >>
    comms_status_t status;   // handler status for concrete debugging
    unsigned int tick;       // tick size measured in miliseconds
    unsigned int tick_limit; // number of ticks for connection integrity
    bool online;      // device is conected
    bool running;     // thread is running
    bool active;      // thread is active
    uint8_t priority; // minimum priority level for message saving
    const char* dev;  // device file path; ej. /dev/tty****
    int fd;           // file descriptor for device
    int id;           // handler ID
    int transactions; // handler transactions comleted
    // << Communication >>
    comms_message_t recv;   // received message
    comms_message_t sent;   // sent message
    // << Thread synchronization >>
    pthread_mutex_t mutex;  // pthread mutex
    pthread_cond_t  cond;   // pthread condition variable
} comms_handler_t;

/*
 * The communication handlers implement serial+protocol communication (CRC-16).
 * Their job is abstract the communication layer, the user interacts with it by setting up
 * requests to send a custom message or to read the next incoming message. 
 * 
 * If no custom message is setup the handler sends a default PING message, 
 * to which connected devices must reply PONG. This requires a continous stream of 
 * message transactions between devices; otherwise the connection becomes deadlocked. 
 * 
 * Each device must be configured to use the same protocol to read (receive) and reply (transmit).
 * 
 * Also, handlers can be configured to only save priority received messages; via the 'priority'
 * field which is compared to 'relevance' field on a protocol frame to determine the filtering
 * criteria. Higher or equal than messages are saved, while those that are less than are taken as
 * update messages to keep the connection alive.
 * 
*/

// ----------------------------------------------------------------------------------
/*
 * Initialize the handler structure for communication
 *
*/
comms_status_t comms_init_handler(
    int id,
    comms_handler_t* handler,
    const char* dev,
    uint8_t priority,
    unsigned int tick,
    unsigned int tick_limit
);

/*
 * Setup handler to begin communication. This creates a detached thread to listen to port.
 * Prints results on console
 *
*/
comms_status_t comms_setup_handler(
    comms_handler_t* handler
);

/*
 * Signal handler to stop and exit thread
 *
*/
comms_status_t comms_stop_handler(
    comms_handler_t* handler
);

// ----------------------------------------------------------------------------------
/*
 * Request handler to setup a custom message to send to device.
 * If handler is currently busy with another message it can't setup more messages.
 *
*/
comms_status_t comms_request_send(
    comms_handler_t* handler,
    const char* message,
    uint8_t priority
);

/*
 * Signal handler to setup for another custom message. It allows writing more custom messages.
 *
*/
comms_status_t comms_acquire_send(
    comms_handler_t* handler
);

/*
 * Request handler to forcefully save the next received message
 *
*/
comms_status_t comms_request_receive(
    comms_handler_t* handler
);

/*
 * Signal handler to allow another incoming message to be saved
 *
*/
comms_status_t comms_acquire_receive(
    comms_handler_t* handler
);

// ----------------------------------------------------------------------------------
/*
 * Communication handler worker function.
 * This functions handles the protocol and serial interface,
 *
*/
void* comms_handler(void* arg);

#ifdef __cplusplus
}
#endif
// ==================================================================================


#endif // HOST_COMMS_H