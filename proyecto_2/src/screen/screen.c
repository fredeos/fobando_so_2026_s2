#include "screen/screen.h"

#include <stdlib.h>

/*
 * Cascarón de la biblioteca de pantalla.
 *
 * La implementación concreta del acceso al hardware se conectará aquí con el
 * device driver. El usuario de la biblioteca solo verá las operaciones públicas
 * declaradas en screen.h.
 */
struct screen {
    screen_state_t state;
};

static int image_valid(const screen_image_t *image) {
    return image != NULL && image->data != NULL && image->size > 0 &&
           image->width > 0 && image->height > 0 &&
           (image->channels == 1 || image->channels == 3 || image->channels == 4);
}

screen_status_t screen_create(screen_t **screen) {
    if (screen == NULL) {
        return SCREEN_INVALID_ARGUMENT;
    }

    *screen = calloc(1, sizeof(**screen));
    if (*screen == NULL) {
        return SCREEN_ERROR;
    }

    (*screen)->state = SCREEN_STATE_CLOSED;
    return SCREEN_OK;
}

screen_status_t screen_destroy(screen_t *screen) {
    if (screen == NULL) {
        return SCREEN_INVALID_ARGUMENT;
    }

    free(screen);
    return SCREEN_OK;
}

screen_status_t screen_init(screen_t *screen) {
    if (screen == NULL) {
        return SCREEN_INVALID_ARGUMENT;
    }

    screen->state = SCREEN_STATE_READY;
    return SCREEN_OK;
}

screen_status_t screen_write(screen_t *screen, const screen_image_t *image) {
    if (screen == NULL || !image_valid(image)) {
        return SCREEN_INVALID_ARGUMENT;
    }
    if (screen->state == SCREEN_STATE_CLOSED) {
        return SCREEN_NOT_INITIALIZED;
    }

    /* TODO: entregar la imagen al driver mediante el protocolo definido. */
    screen->state = SCREEN_STATE_BUSY;
    return SCREEN_NOT_IMPLEMENTED;
}

screen_status_t screen_read(screen_t *screen, uint8_t *data, size_t capacity,
                            size_t *read_size) {
    if (screen == NULL || data == NULL || capacity == 0 || read_size == NULL) {
        return SCREEN_INVALID_ARGUMENT;
    }
    if (screen->state == SCREEN_STATE_CLOSED) {
        return SCREEN_NOT_INITIALIZED;
    }

    *read_size = 0;
    /* TODO: solicitar y leer una respuesta del driver. */
    return SCREEN_NOT_IMPLEMENTED;
}

screen_status_t screen_status(const screen_t *screen, screen_state_t *state) {
    if (screen == NULL || state == NULL) {
        return SCREEN_INVALID_ARGUMENT;
    }

    *state = screen->state;
    return SCREEN_OK;
}

screen_status_t screen_clear(screen_t *screen) {
    if (screen == NULL) {
        return SCREEN_INVALID_ARGUMENT;
    }
    if (screen->state == SCREEN_STATE_CLOSED) {
        return SCREEN_NOT_INITIALIZED;
    }

    /* TODO: enviar al driver la orden de limpiar la pantalla. */
    return SCREEN_NOT_IMPLEMENTED;
}

screen_status_t screen_close(screen_t *screen) {
    if (screen == NULL) {
        return SCREEN_INVALID_ARGUMENT;
    }

    screen->state = SCREEN_STATE_CLOSED;
    return SCREEN_OK;
}
