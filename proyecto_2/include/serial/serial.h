#ifndef COMMON_SERIAL_H
#define COMMON_SERIAL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define SERIAL_BAUDRATE 115200

/* NOTE: this header file serves as base interface for different implementations for serial communication,
for example a Linux device will use its own implementation while microntrollers will implement their own*/

/* 
 * Serial error numbers for operations.
 * Serial operations might throw this at output
*/
typedef enum {
    SERIAL_OK = 0,
    // general errors
    SERIAL_FAILURE = -1,
    SERIAL_NO_DATA = -2,
    SERIAL_INVALID_ARG = -3,
    SERIAL_INVALID_MODE = -4,
    SERIAL_DISCONNECT = -5
} serial_errno_t;

/* 
 * Serial error numbers for operations.
 * Serial operations might throw this at output
*/
typedef enum {
    SERIAL_IN  = 1,
    SERIAL_OUT = 2
} serial_packet_mode_t;

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================================
/* 
 * Structure to hold a serial data packet.
 * Operations might use packets to send or receive byte streams.
 * 
*/
typedef struct {
    serial_packet_mode_t mode; // packet mode: either input or output
    uint8_t *buffer;  // byte buffer
    size_t length;    // number of bytes in buffer
    size_t processed; // number of processed bytes
    bool complete;    // indicator for full packet processed
} serial_packet_t;
// ============================================================================================
/*
 * Load device specific configurations for serial communication interface
 * 
 * _obj: pointer to data or structure where configurations will be saved
 * _config: point to data or structure to extract configurations
 * 
*/
serial_errno_t serial_configure(void* _obj, void* _config);

// ============================================================================================
/* 
 * Write content via serial interface. Automatically returns if packet already complete.
 * Packet mode must be SERIAL_OUT.
 * 
 * _obj: pointer to data or structure for serial configuration (implementation dependant)
 * _packet: data packet
 * _wd_bytes: number of written bytes
 * 
 * Returns SERIAL_OK on success. otherwise throws error.
*/
serial_errno_t serial_write(
    void *_obj, 
    serial_packet_t *_packet, 
    size_t* wd_bytes
);

/* 
 * Read content via serial interface. Automatically returns if packet already complete.
 * Packet mode must be SERIAL_IN.
 * 
 * _obj: pointer to data or structure for serial configuration (implementation dependant)
 * _packet: data packet
 * _wd_bytes: number of written bytes
 * 
 * Returns SERIAL_OK on success. otherwise throws error.
*/
serial_errno_t serial_read(
    void *_obj, 
    serial_packet_t *_packet, 
    size_t* rd_bytes
);

/* 
 * Restarts packet completion for new operations. Already implemented (no need for specific implementation)
 * 
 * _packet: data packet
 * 
 * Returns SERIAL_OK on success. otherwise throws error.
*/
serial_errno_t serial_restart_packet(
    serial_packet_t *_packet
);

#ifdef __cplusplus
}
#endif

#endif //COMMON_SERIAL_H