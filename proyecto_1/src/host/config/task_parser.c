#include "host/task_parser.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Guarda un mensaje descriptivo de error.
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
 * Elimina espacios al inicio y al final
 * de una cadena.
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
 * Convierte una cadena a uint32_t.
 * Permite cero, pero rechaza números inválidos.
 */
static bool parse_u32(
    const char *text,
    uint32_t *out
)
{
    if (!isdigit((unsigned char)text[0]))
        return false;

    errno = 0;

    char *end;

    uintmax_t number = strtoumax(
        text,
        &end,
        10
    );

    if (errno != 0 ||
        *end != '\0' ||
        number > UINT32_MAX) {

        return false;
    }

    *out = (uint32_t)number;

    return true;
}

/*
 * Convierte una cadena a int32_t.
 * Se utiliza para la criticidad.
 */
static bool parse_i32(
    const char *text,
    int32_t *out
)
{
    if (*text != '-' &&
        *text != '+' &&
        !isdigit((unsigned char)*text)) {

        return false;
    }

    errno = 0;

    char *end;

    intmax_t number = strtoimax(
        text,
        &end,
        10
    );

    if (errno != 0 ||
        end == text ||
        *end != '\0' ||
        number < INT32_MIN ||
        number > INT32_MAX) {

        return false;
    }

    *out = (int32_t)number;

    return true;
}

/*
 * Convierte una línea CSV en una tarea.
 *
 * Formato:
 * id,tipo,W,T,D,criticidad
 */
static bool parse_task_row(
    char *row,
    Task *task
)
{
    char *fields[6];

    fields[0] = row;

    /* Separar los seis campos */
    for (size_t i = 1u; i < 6u; ++i) {

        char *comma = strchr(
            fields[i - 1u],
            ','
        );

        if (comma == NULL)
            return false;

        *comma = '\0';

        fields[i] = comma + 1;
    }

    /* Rechazar campos adicionales */
    if (strchr(fields[5], ',') != NULL)
        return false;

    /* Eliminar espacios */
    for (size_t i = 0u; i < 6u; ++i)
        fields[i] = strip(fields[i]);

    Task value = {0};

    /* Identificador */
    if (!parse_u32(fields[0], &value.id))
        return false;

    /* Tipo de tarea */
    if (strcmp(fields[1], "P") == 0) {

        value.type = TASK_PERIODIC;

    } else if (strcmp(fields[1], "S") == 0) {

        value.type = TASK_SPORADIC;

    } else {

        return false;
    }

    /* Validación de atributos numéricos */
    if (!parse_u32(fields[2], &value.work) ||
        value.work == 0u ||

        !parse_u32(fields[3], &value.period) ||
        value.period == 0u ||

        !parse_u32(fields[4], &value.deadline) ||
        value.deadline == 0u ||

        !parse_i32(fields[5], &value.criticality)) {

        return false;
    }

    *task = value;

    return true;
}

/*
 * Libera la memoria de una lista de tareas
 * y reinicia sus atributos.
 */
void task_list_free(TaskList *list)
{
    if (list == NULL)
        return;

    free(list->items);

    *list = (TaskList){0};
}

/*
 * Carga las tareas desde un archivo CSV.
 *
 * Valida cada línea y evita identificadores
 * duplicados.
 *
 * Si ocurre un error, libera los recursos
 * reservados durante la lectura.
 */
bool task_parser_load(
    const char *path,
    TaskList *out,
    char *error,
    size_t error_size
)
{
    if (error != NULL && error_size > 0u)
        error[0] = '\0';

    /* Validar argumentos */
    if (out == NULL || path == NULL) {

        error_set(
            error,
            error_size,
            "Argumentos invalidos para task_parser_load"
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

    TaskList loaded = {0};

    size_t allocated = 0u;
    size_t line_number = 0u;

    char *line = NULL;
    size_t capacity = 0u;

    bool success = false;

    /* Procesar cada línea */
    while (getline(&line, &capacity, file) != -1) {

        ++line_number;

        char *row = strip(line);

        /* Ignorar líneas vacías y comentarios */
        if (*row == '\0' || *row == '#')
            continue;

        Task task;

        /* Validar formato de la tarea */
        if (!parse_task_row(row, &task)) {

            error_set(
                error,
                error_size,
                "Linea %zu: se espera "
                "id,P|S,W,T,D,criticidad "
                "con valores validos",
                line_number
            );

            goto done;
        }

        /* Evitar identificadores duplicados */
        for (size_t i = 0u; i < loaded.count; ++i) {

            if (loaded.items[i].id == task.id) {

                error_set(
                    error,
                    error_size,
                    "Linea %zu: ID %u duplicado",
                    line_number,
                    task.id
                );

                goto done;
            }
        }

        /* Ampliar el arreglo cuando sea necesario */
        if (loaded.count == allocated) {

            if (allocated >
                (SIZE_MAX / sizeof(Task)) / 2u) {

                error_set(
                    error,
                    error_size,
                    "Demasiadas tareas"
                );

                goto done;
            }

            size_t next =
                allocated == 0u
                ? 16u
                : allocated * 2u;

            Task *resized = realloc(
                loaded.items,
                next * sizeof(*resized)
            );

            if (resized == NULL) {

                error_set(
                    error,
                    error_size,
                    "Sin memoria para cargar tareas"
                );

                goto done;
            }

            loaded.items = resized;
            allocated = next;
        }

        /* Guardar tarea validada */
        loaded.items[loaded.count++] = task;
    }

    /* Comprobar errores de lectura */
    if (ferror(file)) {

        error_set(
            error,
            error_size,
            "Error leyendo %s",
            path
        );

        goto done;
    }

    /* Transferir la lista al llamador */
    *out = loaded;

    success = true;

done:

    free(line);
    fclose(file);

    /* Liberar tareas si ocurrió un error */
    if (!success)
        task_list_free(&loaded);

    return success;
}