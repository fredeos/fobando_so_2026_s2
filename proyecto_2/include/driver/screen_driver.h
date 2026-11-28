#ifndef DRIVER_SCREEN_DRIVER_H
#define DRIVER_SCREEN_DRIVER_H

#include <stddef.h>
#include <stdint.h>

#include "serial/serial.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SCREEN_DRIVER_WIDTH 128u
#define SCREEN_DRIVER_HEIGHT 160u
#define SCREEN_DRIVER_BAUDRATE SERIAL_BAUDRATE

typedef enum {
    SCREEN_DRIVER_OK = 0,
    SCREEN_DRIVER_ERROR = -1,
    SCREEN_DRIVER_INVALID_ARGUMENT = -2,
    SCREEN_DRIVER_NOT_INITIALIZED = -3,
    SCREEN_DRIVER_PROTOCOL_ERROR = -4,
    SCREEN_DRIVER_DEVICE_REJECTED = -5
} screen_driver_status_t;

/* Host-side driver context; initialize it with screen_driver_open(). */
typedef struct {
    int serial_fd;
    uint32_t next_message_id;
    uint8_t initialized;
} screen_driver_t;

/* Open and configure the POSIX serial connection to the ESP32. */
screen_driver_status_t screen_driver_open(screen_driver_t *driver,
                                          const char *serial_device,
                                          unsigned int baudrate);

/*
 * Transmit a complete 128x160 raw image. channels must be 1 (GRAY8) or 3
 * (BGR888). The driver hides JSON chunking and protocol frames from its caller.
 */
screen_driver_status_t screen_driver_send_image(screen_driver_t *driver,
                                               const uint8_t *pixels,
                                               size_t size,
                                               uint32_t width,
                                               uint32_t height,
                                               uint8_t channels);

/* Close the serial connection. */
screen_driver_status_t screen_driver_close(screen_driver_t *driver);

#ifdef __cplusplus
}
#endif

#endif /* DRIVER_SCREEN_DRIVER_H */
