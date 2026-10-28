#include "host/manual_tasks.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* Longitud máxima de una entrada manual */
#define MANUAL_LINE_MAX 256u

/*
 * Escribe un mensaje de error sin exceder
 * la capacidad del buffer recibido.
 */
static void set_error(
    char *error,
    size_t size,
    const char *fmt,
    ...
)
{
    if (error == NULL || size == 0u)
        return;

    va_list args;

    va_start(args, fmt);
    vsnprintf(error, size, fmt, args);
    va_end(args);
}

/*
 * Elimina espacios al inicio y al final
 * de una cadena.
 */
static char *trim(char *text)
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
 */
static bool parse_u32(
    const char *text,
    uint32_t *out
)
{
    if (!isdigit((unsigned char)*text))
        return false;

    errno = 0;

    char *end = NULL;

    uintmax_t value = strtoumax(
        text,
        &end,
        10
    );

    if (errno != 0 ||
        *end != '\0' ||
        value > UINT32_MAX) {

        return false;
    }

    *out = (uint32_t)value;

    return true;
}

/*
 * Convierte una cadena a int32_t.
 * Permite valores negativos de criticidad.
 */
static bool parse_i32(
    const char *text,
    int32_t *out
)
{
    const char *number = text;

    if (*number == '+' || *number == '-')
        ++number;

    if (!isdigit((unsigned char)*number))
        return false;

    errno = 0;

    char *end = NULL;

    intmax_t value = strtoimax(
        text,
        &end,
        10
    );

    if (errno != 0 ||
        *end != '\0' ||
        value < INT32_MIN ||
        value > INT32_MAX) {

        return false;
    }

    *out = (int32_t)value;

    return true;
}

/*
 * Interpreta una tarea ingresada manualmente.
 *
 * Formato:
 * id,tipo,W,T,D,criticidad
 *
 * No modifica la tarea de salida si
 * ocurre un error de validación.
 */
bool manual_task_parse(
    const char *line,
    Task *out,
    char *error,
    size_t error_size
)
{
    if (error != NULL && error_size > 0u)
        error[0] = '\0';

    /* Validar argumentos */
    if (line == NULL || out == NULL) {

        set_error(
            error,
            error_size,
            "Argumentos invalidos"
        );

        return false;
    }

    /* Verificar longitud */
    size_t length = strlen(line);

    if (length == 0u ||
        length >= MANUAL_LINE_MAX) {

        set_error(
            error,
            error_size,
            "Linea vacia o demasiado larga"
        );

        return false;
    }

    /* Crear copia modificable */
    char buffer[MANUAL_LINE_MAX];

    memcpy(buffer, line, length + 1u);

    char *fields[6] = {0};

    fields[0] = buffer;

    /* Separar los seis campos */
    for (size_t i = 1u; i < 6u; ++i) {

        char *comma = strchr(
            fields[i - 1u],
            ','
        );

        if (comma == NULL) {

            set_error(
                error,
                error_size,
                "Se requieren exactamente 6 campos"
            );

            return false;
        }

        *comma = '\0';

        fields[i] = comma + 1;
    }

    /* Rechazar campos adicionales */
    if (strchr(fields[5], ',') != NULL) {

        set_error(
            error,
            error_size,
            "La linea contiene campos adicionales"
        );

        return false;
    }

    /* Eliminar espacios de cada campo */
    for (size_t i = 0u; i < 6u; ++i)
        fields[i] = trim(fields[i]);

    Task task = {0};

    /* Convertir valores numéricos */
    if (!parse_u32(fields[0], &task.id) ||

        !parse_u32(fields[2], &task.work) ||

        !parse_u32(fields[3], &task.period) ||

        !parse_u32(fields[4], &task.deadline) ||

        !parse_i32(fields[5], &task.criticality)) {

        set_error(
            error,
            error_size,
            "Valor numerico invalido"
        );

        return false;
    }

    /* Interpretar tipo de tarea */
    if (strcmp(fields[1], "P") == 0) {

        task.type = TASK_PERIODIC;

    } else if (strcmp(fields[1], "S") == 0) {

        task.type = TASK_SPORADIC;

    } else {

        set_error(
            error,
            error_size,
            "Tipo invalido: use P o S"
        );

        return false;
    }

    /* Validar atributos */
    if (!task_is_valid(&task)) {

        set_error(
            error,
            error_size,
            "W, T y D deben ser mayores que cero"
        );

        return false;
    }

    *out = task;

    return true;
}

/*
 * Intenta incorporar una tarea.
 *
 * Reutiliza el planificador existente
 * para realizar asignación y admisión.
 */
bool manual_task_submit(
    Planner *planner,
    const Task *task,
    ManualTaskRequest *request
)
{
    /* Validar argumentos y estado */
    if (planner == NULL ||
        task == NULL ||
        request == NULL ||
        request->status == MANUAL_PENDING) {

        return false;
    }

    ManualTaskRequest next = {0};

    next.task = *task;

    /* Ejecutar First-Fit/Worst-Fit y RMS/EDF */
    next.decision = planner_assign_task(
        planner,
        task
    );

    /* Registrar resultado de la admisión */
    next.status = next.decision.accepted
        ? MANUAL_PENDING
        : MANUAL_REJECTED;

    *request = next;

    return true;
}

/*
 * Confirma una tarea pendiente.
 *
 * No realiza comunicación: solo actualiza
 * el estado después de recibir el ACK.
 */
bool manual_task_confirm(
    ManualTaskRequest *request
)
{
    if (request == NULL ||
        request->status != MANUAL_PENDING) {

        return false;
    }

    request->status = MANUAL_CONFIRMED;

    return true;
}

/*
 * Cancela una incorporación pendiente.
 *
 * Localiza el nodo seleccionado y elimina
 * la reserva de la tarea.
 */
bool manual_task_cancel(
    Planner *planner,
    ManualTaskRequest *request
)
{
    if (planner == NULL ||
        request == NULL ||
        request->status != MANUAL_PENDING) {

        return false;
    }

    /* Buscar el nodo donde se reservó capacidad */
    for (size_t i = 0u;
         i < planner->node_count;
         ++i) {

        Node *node = &planner->nodes[i];

        if (node->id == request->decision.node_id &&
            node_remove_task(node, request->task.id)) {

            request->status = MANUAL_CANCELLED;

            return true;
        }
    }

    return false;
}