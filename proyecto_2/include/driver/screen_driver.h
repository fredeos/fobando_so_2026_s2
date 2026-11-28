#ifndef DRIVER_SCREEN_DRIVER_H
#define DRIVER_SCREEN_DRIVER_H

#include <stddef.h>
#include <stdint.h>

#include "common/screen_link_config.h"
#include "serial/serial.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SCREEN_DRIVER_WIDTH 128u
#define SCREEN_DRIVER_HEIGHT 160u
#define SCREEN_DRIVER_BAUDRATE SCREEN_LINK_BAUDRATE

/*
 * Resultados comunes de la API pública. Los errores de protocolo representan
 * fallos de trama/ACK/timeout; DEVICE_REJECTED indica que el ESP32 contestó
 * pero la respuesta de aplicación no fue la aceptación esperada.
 */
typedef enum {
    SCREEN_DRIVER_OK = 0,
    SCREEN_DRIVER_ERROR = -1,
    SCREEN_DRIVER_INVALID_ARGUMENT = -2,
    SCREEN_DRIVER_NOT_INITIALIZED = -3,
    SCREEN_DRIVER_PROTOCOL_ERROR = -4,
    SCREEN_DRIVER_DEVICE_REJECTED = -5
} screen_driver_status_t;

/*
 * Estado privado del driver POSIX. El usuario reserva esta estructura y la
 * inicializa con screen_driver_open(); no debe modificar sus campos mientras
 * la conexión está activa.
 */
typedef struct {
    int serial_fd;
    uint32_t next_message_id;
    uint8_t initialized;
} screen_driver_t;

/*
 * Callback opcional de progreso. Se ejecuta sincrónicamente después del ACK
 * binario de cada chunk. `context` se reenvía sin interpretarlo. El porcentaje
 * representa chunks confirmados, no procesamiento ni dibujo en pantalla.
 */
typedef void (*screen_driver_progress_fn)(uint32_t completed_chunks,
                                          uint32_t total_chunks,
                                          void *context);

/*
 * Abre y configura el puerto POSIX para comunicarse con el ESP32.
 * `baudrate` debe coincidir con la configuración UART del firmware.
 * Devuelve SCREEN_DRIVER_OK si el contexto queda listo para enviar.
 */
screen_driver_status_t screen_driver_open(screen_driver_t *driver,
                                          const char *serial_device,
                                          unsigned int baudrate);

/*
 * Envía una imagen RAW contigua de 128x160. `pixels` no contiene cabecera ni
 * padding; `size` debe ser width*height*channels. Se aceptan 1 canal (GRAY8)
 * o 3 canales (BGR888). La función calcula los chunks, construye los mensajes
 * de aplicación, los encapsula en el protocolo binario y espera la respuesta
 * final del ESP32. No dibuja ni transforma la imagen.
 */
screen_driver_status_t screen_driver_send_image(screen_driver_t *driver,
                                               const uint8_t *pixels,
                                               size_t size,
                                               uint32_t width,
                                               uint32_t height,
                                               uint8_t channels);

/*
 * Igual que screen_driver_send_image(), pero notifica el avance después de
 * cada chunk confirmado por el protocolo. `progress` puede ser NULL.
 */
screen_driver_status_t screen_driver_send_image_with_progress(
    screen_driver_t *driver, const uint8_t *pixels, size_t size,
    uint32_t width, uint32_t height, uint8_t channels,
    screen_driver_progress_fn progress, void *context);

/* Cierra el puerto y deja el contexto desinicializado; es seguro repetirlo. */
screen_driver_status_t screen_driver_close(screen_driver_t *driver);

#ifdef __cplusplus
}
#endif

#endif /* DRIVER_SCREEN_DRIVER_H */
