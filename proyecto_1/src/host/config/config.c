#include "host/config.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Guarda un mensaje de error en el buffer.
 */
static void error_set(
    char *dst,
    size_t size,
    const char *fmt,
    ...
)
{
    if (dst == NULL || size == 0u)
        return;

    va_list args;

    va_start(args, fmt);
    vsnprintf(dst, size, fmt, args);
    va_end(args);
}

/*
 * Elimina espacios en blanco al inicio
 * y al final de una cadena.
 */
static char *strip(char *text)
{
    while (isspace((unsigned char)*text))
        ++text;

    char *end = text + strlen(text);

    while (end > text &&
           isspace((unsigned char)end[-1])) {
        --end;
    }

    *end = '\0';

    return text;
}

/*
 * Convierte una cadena a uint32_t positivo.
 * Rechaza valores inválidos y fuera de rango.
 */
static bool parse_positive_u32(
    const char *text,
    uint32_t *result
)
{
    if (!isdigit((unsigned char)text[0]))
        return false;

    errno = 0;

    char *end;

    uintmax_t value = strtoumax(
        text,
        &end,
        10
    );

    if (errno != 0 ||
        *end != '\0' ||
        value == 0u ||
        value > UINT32_MAX) {

        return false;
    }

    *result = (uint32_t)value;

    return true;
}

/*
 * Copia una cadena comprobando que
 * no supere la capacidad del destino.
 */
static bool copy_value(
    char *dest,
    size_t size,
    const char *value
)
{
    size_t len = strlen(value);

    if (len == 0u || len >= size)
        return false;

    memcpy(dest, value, len + 1u);

    return true;
}

/*
 * Carga la configuración general del Host.
 *
 * Cada línea debe utilizar:
 * clave=valor
 *
 * Se rechazan claves desconocidas,
 * repetidas o con valores incorrectos.
 */
bool config_load(
    const char *path,
    HostConfig *out,
    char *error,
    size_t error_size
)
{
    /* Claves obligatorias */
    static const char *keys[] = {

        "algorithm",
        "heuristic",
        "tick_ms",
        "port_esp32",
        "port_pico",
        "report_every_ticks",
        "missed_reports_limit",
        "tasks_file"

    };

    const size_t key_count =
        sizeof(keys) / sizeof(keys[0]);

    bool seen[sizeof(keys) / sizeof(keys[0])] = {false};

    HostConfig parsed = {0};

    char *line = NULL;

    size_t capacity = 0u;
    size_t number = 0u;

    bool success = false;

    /* Limpiar mensaje de error */
    if (error != NULL && error_size > 0u)
        error[0] = '\0';

    /* Validar argumentos */
    if (path == NULL || out == NULL) {

        error_set(
            error,
            error_size,
            "Argumentos invalidos para config_load"
        );

        return false;
    }

    /* Abrir archivo */
    FILE *file = fopen(path, "r");

    if (file == NULL) {

        error_set(
            error,
            error_size,
            "No se puede abrir %s: %s",
            path,
            strerror(errno)
        );

        return false;
    }

    /* Procesar cada línea */
    while (getline(&line, &capacity, file) != -1) {

        ++number;

        char *key = strip(line);

        /* Ignorar líneas vacías y comentarios */
        if (*key == '\0' ||
            *key == '#' ||
            *key == ';') {

            continue;
        }

        /* Separar clave y valor */
        char *equal = strchr(key, '=');

        if (equal == NULL) {

            error_set(
                error,
                error_size,
                "Linea %zu: se esperaba clave=valor",
                number
            );

            goto done;
        }

        *equal = '\0';

        char *value = strip(equal + 1);

        key = strip(key);

        /* Buscar la clave */
        size_t index;

        for (index = 0u; index < key_count; ++index) {

            if (strcmp(key, keys[index]) == 0)
                break;
        }

        /* Rechazar claves desconocidas */
        if (index == key_count) {

            error_set(
                error,
                error_size,
                "Linea %zu: clave desconocida '%s'",
                number,
                key
            );

            goto done;
        }

        /* Rechazar claves repetidas */
        if (seen[index]) {

            error_set(
                error,
                error_size,
                "Linea %zu: clave duplicada '%s'",
                number,
                key
            );

            goto done;
        }

        seen[index] = true;

        /* Interpretar el parámetro */
        switch (index) {

            case 0:

                if (strcmp(value, "EDF") == 0) {

                    parsed.algorithm = SCHED_EDF;

                } else if (strcmp(value, "RMS") == 0) {

                    parsed.algorithm = SCHED_RMS;

                } else {

                    goto invalid_value;
                }

                break;

            case 1:

                if (strcmp(value, "FIRST_FIT") == 0) {

                    parsed.heuristic =
                        PLACEMENT_FIRST_FIT;

                } else if (strcmp(value, "WORST_FIT") == 0) {

                    parsed.heuristic =
                        PLACEMENT_WORST_FIT;

                } else {

                    goto invalid_value;
                }

                break;

            case 2:

                if (!parse_positive_u32(
                        value,
                        &parsed.tick_ms)) {

                    goto invalid_value;
                }

                break;

            case 3:

                if (!copy_value(
                        parsed.port_esp32,
                        sizeof(parsed.port_esp32),
                        value)) {

                    goto invalid_value;
                }

                break;

            case 4:

                if (!copy_value(
                        parsed.port_pico,
                        sizeof(parsed.port_pico),
                        value)) {

                    goto invalid_value;
                }

                break;

            case 5:

                if (!parse_positive_u32(
                        value,
                        &parsed.report_every_ticks)) {

                    goto invalid_value;
                }

                break;

            case 6:

                if (!parse_positive_u32(
                        value,
                        &parsed.missed_reports_limit)) {

                    goto invalid_value;
                }

                break;

            case 7:

                if (!copy_value(
                        parsed.tasks_file,
                        sizeof(parsed.tasks_file),
                        value)) {

                    goto invalid_value;
                }

                break;
        }

        continue;

invalid_value:

        error_set(
            error,
            error_size,
            "Linea %zu: valor invalido para '%s'",
            number,
            key
        );

        goto done;
    }

    /* Comprobar errores de lectura */
    if (ferror(file)) {

        error_set(
            error,
            error_size,
            "Error leyendo archivo %s",
            path
        );

        goto done;
    }

    /* Verificar parámetros obligatorios */
    for (size_t i = 0u; i < key_count; ++i) {

        if (!seen[i]) {

            error_set(
                error,
                error_size,
                "Falta parametro obligatorio '%s'",
                keys[i]
            );

            goto done;
        }
    }

    /* Guardar configuración validada */
    *out = parsed;

    success = true;

done:

    free(line);
    fclose(file);

    return success;
}