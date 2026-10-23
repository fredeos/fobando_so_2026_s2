// > POSIX serial.h implementation file
#include "serial/serial.h"

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
// =====================================================================================
/*
 * POSIX implementation of serial data write
 *
 * 
*/
serial_errno_t serial_write(
    void *_obj, 
    serial_packet_t *_packet, 
    size_t* wd_bytes
){
    // << Exceptions >>
    if (_obj == NULL || _packet == NULL || wd_bytes == NULL)
        return SERIAL_INVALID_ARG;
    // << Cast object >>
    int* fd = (int*) _obj;
    // << Configure packet >>
    if (_packet->mode != SERIAL_OUT)
        return SERIAL_INVALID_MODE; // Error: invalid packet mode
    *wd_bytes = 0;
    if (_packet->complete)
        return SERIAL_OK;
    // << Attempt to send packet >>
    uint8_t *ptr = _packet->buffer + _packet->processed;
    size_t  rem  = _packet->length - _packet->processed;
    ssize_t n = write(
        *fd,
        ptr,
        rem
    );
    if (n < 0){
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return SERIAL_NO_DATA;
        return SERIAL_FAILURE; // Error: failed to send packet
    }
    // << Detect if full packet sent >>
    size_t wd = (size_t) n;
    _packet->processed += wd;
    _packet->complete = (_packet->processed == _packet->length);
    // << Return >>
    *wd_bytes = wd;
    return SERIAL_OK;
}

// =====================================================================================
/*
 * POSIX implementation of serial data read
 *
 * 
*/
serial_errno_t serial_read(
    void *_obj, 
    serial_packet_t *_packet, 
    size_t* rd_bytes
){  // << Exceptions >>
    if (_obj == NULL || _packet == NULL || rd_bytes == NULL)
        return SERIAL_INVALID_ARG;
    // << Cast object >>
    int* fd = (int*) _obj;
    // << Configure packet >>
    if (_packet->mode != SERIAL_IN)
        return SERIAL_INVALID_MODE; // Error: invalid packet mode
    *rd_bytes = 0;
    if (_packet->complete)
        return SERIAL_OK;
    // << Attempt to send packet >>
    uint8_t *ptr = _packet->buffer + _packet->processed;
    size_t  rem  = _packet->length - _packet->processed;
    ssize_t n = read(
        *fd,
        ptr,
        rem
    );
    if (n < 0){
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return SERIAL_NO_DATA;
        return SERIAL_FAILURE; // Error: failed to receive packet
    }
    // << Detect if full packet sent >>
    size_t rd = (size_t) n;
    _packet->processed += rd;
    _packet->complete = (_packet->processed == _packet->length);
    // << Return >>
    *rd_bytes = rd;
    return SERIAL_OK;
}


// =====================================================================================
/*
 * POSIX implementation of serial data restart
 *
 * 
*/
serial_errno_t serial_restart_packet(
    serial_packet_t *_packet
){
    _packet->processed = 0;
    _packet->complete  = false;
    return SERIAL_OK;
}