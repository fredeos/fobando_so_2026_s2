#ifndef SCREEN_SCREEN_H
#define SCREEN_SCREEN_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Resultado de las operaciones de la biblioteca de pantalla. */
typedef enum {
    SCREEN_OK = 0,
    SCREEN_ERROR = -1,
    SCREEN_INVALID_ARGUMENT = -2,
    SCREEN_NOT_INITIALIZED = -3,
    SCREEN_NOT_IMPLEMENTED = -4
} screen_status_t;

/* Estado observable de la pantalla o de la última transferencia. */
typedef enum {
    SCREEN_STATE_CLOSED = 0,
    SCREEN_STATE_READY,
    SCREEN_STATE_BUSY,
    SCREEN_STATE_ERROR
} screen_state_t;

/* Imagen en formato de píxeles crudos que será mostrada en la pantalla. */
typedef struct {
    const uint8_t *data;
    size_t size;
    uint32_t width;
    uint32_t height;
    uint8_t channels;
} screen_image_t;

/* Contexto opaco: sus detalles no son visibles para el usuario. */
typedef struct screen screen_t;

/* Reserva y libera el contexto opaco de la biblioteca. */
screen_status_t screen_create(screen_t **screen);
screen_status_t screen_destroy(screen_t *screen);

/* Inicializa la biblioteca y la conexión con el driver. */
screen_status_t screen_init(screen_t *screen);

/* Envía una imagen completa; el empaquetado es interno y transparente. */
screen_status_t screen_write(screen_t *screen, const screen_image_t *image);

/* Lee una respuesta o estado proporcionado por el dispositivo. */
screen_status_t screen_read(screen_t *screen, uint8_t *data, size_t capacity,
                            size_t *read_size);

/* Obtiene el estado actual de la pantalla. */
screen_status_t screen_status(const screen_t *screen, screen_state_t *state);

/* Solicita limpiar el contenido mostrado. */
screen_status_t screen_clear(screen_t *screen);

/* Libera los recursos de la biblioteca. */
screen_status_t screen_close(screen_t *screen);

#ifdef __cplusplus
}
#endif

#endif /* SCREEN_SCREEN_H */
