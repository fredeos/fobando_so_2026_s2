#ifndef SERIAL_POSIX_H
#define SERIAL_POSIX_H

/*
 * POSIX serial interface configurations
 *
*/
typedef struct {
    const char* device;
    unsigned int baudrate;
} posix_serial_config_t;

#endif // SERIAL_POSIXH