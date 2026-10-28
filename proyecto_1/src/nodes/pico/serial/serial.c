// > Raspberry Pi Pico serial.h implementation file
#include "serial/serial.h"
#include "pico.h"

#include <stdio.h>
#include "pico/stdlib.h"


// ======================================================================================
/*
 * Raspberry Pi Pico implementation of serial configuration
 *
*/
serial_errno_t serial_configure(
    void* _obj,
    void* _config
){
    // << Exceptions >>
    if (_obj == NULL || _config == NULL)
        return SERIAL_INVALID_ARG;

    // << Get serial objects >>
    pico_serial_t* serial = (pico_serial_t*) _obj;
    pico_serial_configs_t* conf =
        (pico_serial_configs_t*) _config;

    // << Initialize Pico stdio >>
    bool init = stdio_init_all();
    if (!init)
        return SERIAL_FAILURE;

    // << Set configuration >>
    serial->timeout_us = conf->timeout_us;

    // << Return >>
    return SERIAL_OK;
}


// ======================================================================================
/*
 * Raspberry Pi Pico implementation of serial data write
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
    pico_serial_t* serial = (pico_serial_t*) _obj;
    (void)serial;

    // << Configure packet >>
    if (_packet->mode != SERIAL_OUT)
        return SERIAL_INVALID_MODE;

    *wd_bytes = 0;
    if (_packet->complete)
        return SERIAL_OK;
    // << Attempt to send packet >>
    uint8_t *ptr = _packet->buffer + _packet->processed;
    size_t rem = _packet->length - _packet->processed;

    // << Send bytes >>
    size_t wd = 0;
    while (wd < rem) {
        int result = putchar_raw(ptr[wd]);

        if (result == EOF)
            return SERIAL_FAILURE;

        wd++;
    }
    if (wd == 0)
        return SERIAL_NO_DATA;

    // << Detect if full packet sent >>
    _packet->processed += wd;
    _packet->complete = (_packet->processed == _packet->length);

    // << Return >>
    *wd_bytes = wd;
    return SERIAL_OK;
}


// ======================================================================================
/*
 * Raspberry Pi Pico implementation of serial data read
 *
*/
serial_errno_t serial_read(
    void *_obj,
    serial_packet_t *_packet,
    size_t* rd_bytes
){
    // << Exceptions >>
    if (_obj == NULL || _packet == NULL || rd_bytes == NULL)
        return SERIAL_INVALID_ARG;
    // << Cast object >>
    pico_serial_t* serial = (pico_serial_t*) _obj;

    // << Configure packet >>
    if (_packet->mode != SERIAL_IN)
        return SERIAL_INVALID_MODE;

    *rd_bytes = 0;
    if (_packet->complete)
        return SERIAL_OK;
    // << Attempt to receive packet >>
    uint8_t *ptr =_packet->buffer + _packet->processed;
    size_t rem = _packet->length - _packet->processed;

    // << Read available bytes >>
    size_t rd = 0;
    while (rd < rem) {

        int ch = getchar_timeout_us(serial->timeout_us);

        if (ch == PICO_ERROR_TIMEOUT)
            break;

        ptr[rd] = (uint8_t)ch;
        rd++;
    }

    // << No data received >>
    if (rd == 0)
        return SERIAL_NO_DATA;

    // << Detect if full packet received >>
    _packet->processed += rd;
    _packet->complete = (_packet->processed == _packet->length);

    // << Return >>
    *rd_bytes = rd;
    return SERIAL_OK;
}


// ======================================================================================
/*
 * Raspberry Pi Pico implementation of serial data restart
 *
*/
serial_errno_t serial_restart_packet(
    serial_packet_t *_packet
){
    if (_packet == NULL)
        return SERIAL_INVALID_ARG;

    _packet->processed = 0;
    _packet->complete = false;

    return SERIAL_OK;
}