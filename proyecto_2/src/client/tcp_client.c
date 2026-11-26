#include "client/header.h"
#include "common/tcp.h"
#include "common/image_job.h"
#include "filters/filters.h"
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <arpa/inet.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_port(const char *text, uint16_t *port) {
    unsigned int value = 0;
    if (*text == '\0')
        return -1;
    for (const char *p = text; *p != '\0'; ++p) {
        if (*p < '0' || *p > '9')
            return -1;
        value = value * 10 + (unsigned int)(*p - '0');
        if (value > UINT16_MAX)
            return -1;
    }
    if (value == 0)
        return -1;
    *port = (uint16_t)value;
    return 0;
}

static int read_image(const char *path, uint8_t **data, size_t *size) {
    FILE *file = fopen(path, "rb"); // Lee PNG/JPEG como bytes, sin interpretar todavia sus pixeles.
    uint8_t *buffer = NULL;
    long length;
    int error = EIO;

    *data = NULL;
    *size = 0;
    if (file == NULL)
        return -1;
    if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0)
        goto fail;
    if (length == 0 || (unsigned long)length > TCP_MAX_PAYLOAD) { // Impide enviar archivos vacios o superiores a 8 MiB.
        error = EMSGSIZE;
        goto fail;
    }
    if (fseek(file, 0, SEEK_SET) != 0)
        goto fail;
    buffer = malloc((size_t)length);
    if (buffer == NULL) {
        error = ENOMEM;
        goto fail;
    }
    if (fread(buffer, 1, (size_t)length, file) != (size_t)length || fgetc(file) != EOF ||
        ferror(file))
        goto fail;
    if (fclose(file) != 0) {
        file = NULL;
        goto fail;
    }
    *data = buffer;
    *size = (size_t)length;
    return 0;

fail:
    if (file != NULL)
        fclose(file);
    if (buffer != NULL)
        crypto_clear(buffer, (size_t)length);
    free(buffer);
    errno = error;
    return -1;
}

static int number(const char *text, uint32_t *out) {
    uint32_t value = 0;
    if (!text || !*text)
        return -1;
    for (const char *p = text; *p; ++p) {
        if (*p < '0' || *p > '9')
            return -1;
        unsigned d = (unsigned)(*p - '0');
        if (value > (UINT32_MAX - d) / 10) // Detecta desbordamiento antes de convertir un argumento numerico.
            return -1;
        value = value * 10 + d;
    }
    *out = value;
    return 0;
}
static int resize_arg(const char *text, image_job *job) {
    if (strcmp(text, "original") == 0) { // Las dimensiones cero solicitan conservar el tamano original.
        job->width = 0;
        job->height = 0;
        return 0;
    }
    const char *x = strchr(text, 'x'); // Separa ancho y alto del argumento, por ejemplo 128x160.
    if (!x || x == text || (size_t)(x - text) > 10)
        return -1;
    char width[11];
    memcpy(width, text, (size_t)(x - text));
    width[x - text] = 0;
    return number(width, &job->width) == 0 && number(x + 1, &job->height) == 0 &&
                   image_dimensions_valid(job->width, job->height)
               ? 0
               : -1;
}
static int save_result(const char *path, const char *input, const uint8_t *data, size_t size) {
    struct stat a, b;
    if (stat(input, &a) == 0 && stat(path, &b) == 0 && a.st_dev == b.st_dev &&
        a.st_ino == b.st_ino) { // Evita reemplazar la imagen de entrada con el resultado.
        errno = EINVAL;
        return -1;
    }
    size_t length = strlen(path);
    if (length > SIZE_MAX - 12) {
        errno = ENAMETOOLONG;
        return -1;
    }
    char *temporary = malloc(length + 12);
    if (!temporary) {
        errno = ENOMEM;
        return -1;
    }
    snprintf(temporary, length + 12, "%s.tmp.XXXXXX", path);
    int fd = mkstemp(temporary); // Crea un archivo temporal exclusivo para no dejar un PNG parcial.
    if (fd < 0) {
        free(temporary);
        return -1;
    }
    FILE *file = fdopen(fd, "wb");
    int result = -1;
    if (!file) {
        close(fd);
        goto cleanup;
    }
    int failed = fwrite(data, 1, size, file) != size;
    if (fclose(file) != 0)
        failed = 1;
    if (!failed && rename(temporary, path) == 0) // Publica el resultado completo mediante un renombrado atomico.
        result = 0;
cleanup: {
    int saved = errno;
    if (result < 0)
        unlink(temporary);
    free(temporary);
    errno = saved;
}
    return result;
}
int tcp_client_run(int argc, char **argv) {
    image_job job = {FILTER_BLUR, 0, 0, 16, 0, 1, 2}; // Valores por defecto: blur, tamano original y bloques de 16 filas.
    const char *output = "output/processed.png";
    uint16_t port = 0;
    int partition = 0;
    int legacy = argc == 5 && strcmp(argv[4], "--ack-only") == 0; // Conserva la prueba antigua que solo confirma recepcion.
    int valid = argc >= 4 && parse_port(argv[2], &port) == 0;
    for (int i = 4; valid && !legacy && i < argc; i += 2) { // Procesa pares opcion/valor sin mezclar parametros con pixeles.
        if (i + 1 >= argc) {
            valid = 0;
            break;
        }
        const char *option = argv[i], *value = argv[i + 1];
        if (strcmp(option, "--filter") == 0) {
            if (strcmp(value, "blur") == 0)
                job.filter = FILTER_BLUR;
            else if (strcmp(value, "sharpen") == 0)
                job.filter = FILTER_SHARPEN;
            else if (strcmp(value, "sobel") == 0)
                job.filter = FILTER_SOBEL;
            else
                valid = 0;
        } else if (strcmp(option, "--resize") == 0)
            valid = resize_arg(value, &job) == 0;
        else if (strcmp(option, "--output") == 0)
            output = value;
        else {
            uint32_t n;
            if (number(value, &n) < 0) {
                valid = 0;
                break;
            }
            if (strcmp(option, "--radius") == 0)
                job.radius = n;
            else if (strcmp(option, "--intensity") == 0)
                job.intensity = n;
            else if (strcmp(option, "--block-rows") == 0) {
                if (partition || !n) // No permite combinar --blocks con --block-rows ni usar cero.
                    valid = 0;
                job.block_rows = n;
                partition = 1;
            } else if (strcmp(option, "--blocks") == 0) {
                if (partition || !n) // No permite combinar --blocks con --block-rows ni usar cero.
                    valid = 0;
                job.blocks = n;
                job.block_rows = 0;
                partition = 1;
            } else
                valid = 0;
        }
    }
    if (!valid || image_job_valid(&job, 1) < 0) {
        fprintf(stderr,
                "Uso: %s IP PUERTO IMAGEN [--filter blur|sharpen|sobel] [--resize "
                "ANCHOxALTO|original]\n"
                "       [--output SALIDA.png] [--block-rows N | --blocks N] [--radius 1..4] "
                "[--intensity 0..16]\n",
                argv[0]);
        return EXIT_FAILURE;
    }
    uint8_t key[CRYPTO_KEY_SIZE] = {0}, *image = NULL;
    size_t image_size = 0;
    int fd = -1, result = EXIT_FAILURE;
    tcp_message response = {0};
    if (crypto_load_key(key) < 0) { // Rechaza una clave ausente o mal formada antes de conectarse.
        fprintf(stderr, "DIPS_AES_KEY debe contener 64 digitos hexadecimales.\n");
        goto cleanup;
    }
    if (read_image(argv[3], &image, &image_size) < 0) {
        perror("Lectura imagen (1 byte a 8 MiB)");
        goto cleanup;
    }
    fd = tcp_connect(argv[1], port); // Abre la conexion IPv4 con el coordinador del cluster.
    if (fd < 0) {
        perror("Conexion IPv4");
        goto cleanup;
    }
    struct timeval timeout = {120, 0}; // Limita la espera ante un servidor que deja de responder.
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0 ||
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0) {
        perror("Timeout TCP");
        goto cleanup;
    }
    if (!legacy) {
        uint8_t request[IMAGE_JOB_HEADER_SIZE];
        if (image_job_encode(&job, request) < 0 || // Prepara primero los parametros del procesamiento solicitado.
            tcp_send_message(fd, key, TCP_MESSAGE_PROCESS, request, sizeof(request)) < 0) {
            perror("Solicitud TCP");
            goto cleanup;
        }
    }
    if (tcp_send_message(fd, key, TCP_MESSAGE_IMAGE, image, image_size) < 0) { // Envia el archivo binario dentro de una trama AES-GCM.
        perror("Envio imagen");
        goto cleanup;
    }
    if (tcp_recv_message(fd, key, &response) < 0) { // Recibe el PNG de vuelta y verifica su autenticidad.
        perror("Recepcion/autenticacion resultado");
        goto cleanup;
    }
    if (response.type == TCP_MESSAGE_ERROR) { // Distingue un rechazo del servidor de un resultado valido.
        fprintf(stderr, "Error del servidor: ");
        for (uint32_t i = 0; i < response.payload_size && i < 512; ++i) {
            unsigned c = response.payload[i];
            fputc(c >= 32 && c <= 126 ? (int)c : '?', stderr);
        }
        fputc('\n', stderr);
        goto cleanup;
    }
    if (legacy) {
        const uint8_t ack[] = {0x4f, 0x4b};
        if (response.type != TCP_MESSAGE_ACK || response.payload_size != 2 ||
            memcmp(response.payload, ack, 2) != 0) {
            fprintf(stderr, "ACK inesperado\n");
            goto cleanup;
        }
        printf("Exito: imagen de %zu bytes enviada y ACK autenticado.\n", image_size);
    } else {
        const uint8_t signature[] = {137, 80, 78, 71, 13, 10, 26, 10}; // Firma binaria del formato PNG que debe devolver el cluster.
        if (response.type != TCP_MESSAGE_RESULT || response.payload_size < 33 ||
            memcmp(response.payload, signature, 8) != 0 ||
            memcmp(response.payload + 12, "IHDR", 4) != 0) {
            fprintf(stderr, "Resultado PNG inesperado\n");
            goto cleanup;
        }
        uint32_t width, height;
        memcpy(&width, response.payload + 16, 4);
        memcpy(&height, response.payload + 20, 4);
        width = ntohl(width);
        height = ntohl(height);
        if (!image_dimensions_valid(width, height) || // Comprueba que la resolucion recibida coincide con lo solicitado.
            (job.width && (job.width != width || job.height != height))) {
            fprintf(stderr, "Dimensiones PNG inesperadas\n");
            goto cleanup;
        }
        if (save_result(output, argv[3], response.payload, response.payload_size) < 0) { // Guarda en el cliente solo despues de autenticar y validar el PNG.
            perror("Guardar PNG (ruta distinta a la entrada)");
            goto cleanup;
        }
        printf("Resultado autenticado guardado en %s: %u bytes.\n", output, response.payload_size);
    }
    result = EXIT_SUCCESS;
cleanup:
    tcp_message_free(&response);
    tcp_close(&fd);
    crypto_clear(image, image_size);
    free(image);
    crypto_clear(key, sizeof(key)); // Borra la copia local de la clave antes de terminar.
    return result;
}
