#ifndef HOST_COMMS_H
#define HOST_COMMS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <pthread.h>

#include "common/protocol.h"
#include "serial/serial.h"
#include "./../serial/posix.h"

/*
 *
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
 *
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
 *
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
 * Request handler to setup a custom message to send to device
 *
*/
comms_status_t comms_request_send(
    comms_handler_t* handler,
    const char* message,
    uint8_t priorirty
);

/*
 * 
 *
*/
comms_status_t comms_acquire_send(
    comms_handler_t* handler
);

/*
 *
 *
*/
comms_status_t comms_request_receive(
    comms_handler_t* handler
);

/*
 * 
 *
*/
comms_status_t comms_acquire_receive(
    comms_handler_t* handler
);

// ----------------------------------------------------------------------------------
/*
 *
 *
*/
void* comms_handler(void* arg);

#ifdef __cplusplus
}
#endif
// ==================================================================================


#endif // HOST_COMMS_H