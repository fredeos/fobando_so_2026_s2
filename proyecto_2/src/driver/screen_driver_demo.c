#include "driver/screen_driver.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_channels(const char *text, uint8_t *channels) {
    if (strcmp(text, "1") == 0) {
        *channels = 1u;
        return 0;
    }
    if (strcmp(text, "3") == 0) {
        *channels = 3u;
        return 0;
    }
    return -1;
}

static int read_raw_image(const char *path, uint8_t *pixels, size_t expected_size) {
    FILE *file = fopen(path, "rb");
    if (file == NULL)
        return -1;

    size_t read_size = fread(pixels, 1u, expected_size, file);
    int extra = fgetc(file);
    int failed = read_size != expected_size || extra != EOF || ferror(file);
    int saved_errno = errno;
    if (fclose(file) != 0 && !failed) {
        failed = 1;
        saved_errno = errno;
    }
    if (failed) {
        errno = saved_errno != 0 ? saved_errno : EINVAL;
        return -1;
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 3 || argc > 4) {
        fprintf(stderr, "Uso: %s DISPOSITIVO_SERIAL IMAGEN_RAW [CANALES: 1|3]\n", argv[0]);
        fprintf(stderr, "La imagen debe ser 128x160, sin cabecera; canales por defecto: 3 (BGR888).\n");
        return EXIT_FAILURE;
    }

    uint8_t channels = 3u;
    if (argc == 4 && parse_channels(argv[3], &channels) < 0) {
        fprintf(stderr, "Cantidad de canales invalida; use 1 (GRAY8) o 3 (BGR888).\n");
        return EXIT_FAILURE;
    }

    const size_t image_size = (size_t)SCREEN_DRIVER_WIDTH * SCREEN_DRIVER_HEIGHT * channels;
    uint8_t *pixels = malloc(image_size);
    if (pixels == NULL) {
        perror("Reservar memoria para la imagen");
        return EXIT_FAILURE;
    }

    if (read_raw_image(argv[2], pixels, image_size) < 0) {
        fprintf(stderr, "No se pudo leer %s como imagen raw de %zu bytes: %s\n", argv[2],
                image_size, strerror(errno));
        free(pixels);
        return EXIT_FAILURE;
    }

    screen_driver_t driver = {0};
    screen_driver_status_t status = screen_driver_open(&driver, argv[1],
                                                       SCREEN_DRIVER_BAUDRATE);
    if (status != SCREEN_DRIVER_OK) {
        fprintf(stderr, "No se pudo abrir el dispositivo serial (codigo %d).\n", status);
        free(pixels);
        return EXIT_FAILURE;
    }

    printf("UART abierto: %s a %u baudios. Enviando imagen 128x160 (%s, %zu bytes)...\n",
           argv[1], SCREEN_DRIVER_BAUDRATE, channels == 1u ? "GRAY8" : "BGR888", image_size);
    status = screen_driver_send_image(&driver, pixels, image_size, SCREEN_DRIVER_WIDTH,
                                      SCREEN_DRIVER_HEIGHT, channels);
    if (status == SCREEN_DRIVER_OK)
        puts("Imagen aceptada por el ESP32.");
    else
        fprintf(stderr, "Fallo al enviar/confirmar la imagen (codigo %d).\n", status);

    screen_driver_status_t close_status = screen_driver_close(&driver);
    free(pixels);
    if (close_status != SCREEN_DRIVER_OK) {
        fprintf(stderr, "No se pudo cerrar correctamente el dispositivo serial.\n");
        return EXIT_FAILURE;
    }
    puts("UART cerrado.");
    return status == SCREEN_DRIVER_OK ? EXIT_SUCCESS : EXIT_FAILURE;
}
