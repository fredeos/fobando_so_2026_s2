#include "common/message_parser.h"

#include <stdbool.h>
#include <string.h>

#include "core_json.h"

/* Propagar el primer error sin publicar un mensaje parcialmente interpretado */
#define PARSE_TRY(operation) do { \
    MessageParseResult result_ = (operation); \
    if (result_ != MESSAGE_PARSE_OK) return result_; \
} while (0)

#define FIELD_COUNT(fields) (sizeof(fields) / sizeof((fields)[0]))

/* Campo esperado y vista temporal de su valor dentro del string original */
typedef struct {
    const char *name;
    JSONPair_t pair;
} MessageField;

/* Nombres de aplicación; no dependen de la representación binaria del enum */
static const struct {
    MessageType type;
    const char *name;
} message_types[] = {
    {MSG_CONFIG, "CONFIG"},
    {MSG_CALIBRATE_REQUEST, "CALIBRATE_REQUEST"},
    {MSG_CALIBRATE_RESPONSE, "CALIBRATE_RESPONSE"},
    {MSG_DEPLOY_TASK, "DEPLOY_TASK"},
    {MSG_START, "START"},
    {MSG_EVENT, "EVENT"},
    {MSG_TICK_REPORT, "TICK_REPORT"},
    {MSG_TRIGGER_SPORADIC, "TRIGGER_SPORADIC"},
    {MSG_STOP, "STOP"},
    {MSG_ACK, "ACK"},
    {MSG_NACK, "NACK"}
};

/* Comparar una vista sin terminador NUL con un nombre del contrato */
static bool text_equals(const char *text, size_t length, const char *name)
{
    return length == strlen(name) && memcmp(text, name, length) == 0;
}

/*
 * Lee los campos de un objeto previamente validado por coreJSON.
 * Todos son obligatorios; rechaza nombres desconocidos o repetidos.
 * Los nombres se comparan literalmente, sin decodificar escapes JSON.
 */
static MessageParseResult read_object(
    const char *text,
    size_t length,
    MessageField *fields,
    size_t count
)
{
    size_t start = 0u;
    size_t next = 0u;
    JSONPair_t pair = {0};
    JSONStatus_t status;

    while (start < length &&
           (text[start] == ' ' || text[start] == '\t' ||
            text[start] == '\r' || text[start] == '\n')) {
        ++start;
    }

    if (start == length || text[start] != '{')
        return MESSAGE_PARSE_INVALID_SCHEMA;

    while ((status = JSON_Iterate(text, length, &start, &next, &pair))
           == JSONSuccess) {
        size_t i;

        for (i = 0u; i < count; ++i) {
            if (text_equals(pair.key, pair.keyLength, fields[i].name))
                break;
        }

        if (i == count || fields[i].pair.value != NULL)
            return MESSAGE_PARSE_INVALID_SCHEMA;

        fields[i].pair = pair;
    }

    if (status != JSONNotFound)
        return MESSAGE_PARSE_INVALID_SCHEMA;

    for (size_t i = 0u; i < count; ++i) {
        if (fields[i].pair.value == NULL)
            return MESSAGE_PARSE_INVALID_SCHEMA;
    }

    return MESSAGE_PARSE_OK;
}

/*
 * Convierte dígitos decimales con límite explícito.
 * Evita strto* porque la entrada no necesariamente termina en NUL.
 * No usa double, para conservar todos los bits de los ticks uint64_t.
 */
static MessageParseResult read_magnitude(
    const char *text,
    size_t length,
    uint64_t limit,
    uint64_t *out
)
{
    uint64_t value = 0u;

    if (length == 0u)
        return MESSAGE_PARSE_INVALID_SCHEMA;

    for (size_t i = 0u; i < length; ++i) {
        if (text[i] < '0' || text[i] > '9')
            return MESSAGE_PARSE_INVALID_SCHEMA;

        unsigned digit = (unsigned)(text[i] - '0');

        if (value > limit / 10u ||
            (value == limit / 10u && digit > limit % 10u)) {
            return MESSAGE_PARSE_OUT_OF_RANGE;
        }

        value = value * 10u + digit;
    }

    *out = value;
    return MESSAGE_PARSE_OK;
}

/* Leer un entero sin signo en el rango indicado por el campo */
static MessageParseResult read_unsigned(
    const MessageField *field,
    uint64_t limit,
    uint64_t *out
)
{
    if (field->pair.jsonType != JSONNumber)
        return MESSAGE_PARSE_INVALID_SCHEMA;

    return read_magnitude(
        field->pair.value, field->pair.valueLength, limit, out
    );
}

/* Leer un campo uint32_t, permitiendo o rechazando el valor cero */
static MessageParseResult read_u32(
    const MessageField *field,
    bool positive,
    uint32_t *out
)
{
    uint64_t value;

    PARSE_TRY(read_unsigned(field, UINT32_MAX, &value));

    if (positive && value == 0u)
        return MESSAGE_PARSE_OUT_OF_RANGE;

    *out = (uint32_t)value;
    return MESSAGE_PARSE_OK;
}

/* Leer criticidad o detalle, incluyendo INT32_MIN sin desbordamiento */
static MessageParseResult read_i32(const MessageField *field, int32_t *out)
{
    if (field->pair.jsonType != JSONNumber)
        return MESSAGE_PARSE_INVALID_SCHEMA;

    const char *text = field->pair.value;
    size_t length = field->pair.valueLength;
    bool negative = length > 0u && text[0] == '-';
    uint64_t magnitude;
    uint64_t limit = (uint64_t)INT32_MAX + (negative ? 1u : 0u);

    PARSE_TRY(read_magnitude(
        text + (negative ? 1u : 0u),
        length - (negative ? 1u : 0u), limit, &magnitude
    ));

    *out = negative ? (int32_t)(-(int64_t)magnitude) : (int32_t)magnitude;
    return MESSAGE_PARSE_OK;
}

/* Interpretar un nombre de MessageType del contrato JSON */
static MessageParseResult read_message_type(
    const MessageField *field,
    MessageType *out
)
{
    if (field->pair.jsonType != JSONString)
        return MESSAGE_PARSE_INVALID_SCHEMA;

    for (size_t i = 0u; i < FIELD_COUNT(message_types); ++i) {
        if (text_equals(field->pair.value, field->pair.valueLength,
                        message_types[i].name)) {
            *out = message_types[i].type;
            return MESSAGE_PARSE_OK;
        }
    }

    return MESSAGE_PARSE_UNKNOWN_TYPE;
}

/* Leer la definición existente de Task sin depender del planificador */
static MessageParseResult read_task(const MessageField *field, Task *out)
{
    MessageField fields[] = {
        {"id", {0}}, {"type", {0}}, {"work", {0}},
        {"period", {0}}, {"deadline", {0}}, {"criticality", {0}}
    };

    if (field->pair.jsonType != JSONObject)
        return MESSAGE_PARSE_INVALID_SCHEMA;

    PARSE_TRY(read_object(field->pair.value, field->pair.valueLength,
                          fields, FIELD_COUNT(fields)));
    PARSE_TRY(read_u32(&fields[0], false, &out->id));

    if (fields[1].pair.jsonType != JSONString ||
        fields[1].pair.valueLength != 1u ||
        (fields[1].pair.value[0] != 'P' && fields[1].pair.value[0] != 'S')) {
        return MESSAGE_PARSE_INVALID_SCHEMA;
    }

    out->type = (TaskType)fields[1].pair.value[0];
    PARSE_TRY(read_u32(&fields[2], true, &out->work));
    PARSE_TRY(read_u32(&fields[3], true, &out->period));
    PARSE_TRY(read_u32(&fields[4], true, &out->deadline));
    return read_i32(&fields[5], &out->criticality);
}

/* Interpretar los parámetros de configuración propios del nodo */
static MessageParseResult read_config(const JSONPair_t *data, Message *out)
{
    MessageField fields[] = {
        {"algorithm", {0}}, {"tick_ms", {0}}, {"report_every_ticks", {0}}
    };

    PARSE_TRY(read_object(data->value, data->valueLength,
                          fields, FIELD_COUNT(fields)));

    if (fields[0].pair.jsonType != JSONString)
        return MESSAGE_PARSE_INVALID_SCHEMA;

    if (text_equals(fields[0].pair.value, fields[0].pair.valueLength, "RMS"))
        out->data.config.algorithm = MESSAGE_SCHED_RMS;
    else if (text_equals(fields[0].pair.value, fields[0].pair.valueLength, "EDF"))
        out->data.config.algorithm = MESSAGE_SCHED_EDF;
    else
        return MESSAGE_PARSE_INVALID_SCHEMA;

    PARSE_TRY(read_u32(&fields[1], true, &out->data.config.tick_ms));
    return read_u32(&fields[2], true, &out->data.config.report_every_ticks);
}

/* Leer un evento del nodo; el Host agregará su propia marca temporal */
static MessageParseResult read_event(const JSONPair_t *data, Message *out)
{
    MessageField fields[] = {
        {"event_type", {0}}, {"task_id", {0}}, {"job_id", {0}},
        {"tick", {0}}, {"duration_ticks", {0}}, {"detail", {0}}
    };
    Event *event = &out->data.event;
    uint32_t type;

    PARSE_TRY(read_object(data->value, data->valueLength,
                          fields, FIELD_COUNT(fields)));
    PARSE_TRY(read_u32(&fields[0], true, &type));

    if (type < EVENT_TASK_ACCEPTED || type > EVENT_TASK_SWITCH)
        return MESSAGE_PARSE_OUT_OF_RANGE;

    event->type = (EventType)type;
    event->source = EVENT_SOURCE_NODE;
    event->node_id = out->node_id;
    PARSE_TRY(read_u32(&fields[1], false, &event->task_id));
    PARSE_TRY(read_u32(&fields[2], false, &event->job_id));
    PARSE_TRY(read_unsigned(&fields[3], UINT64_MAX, &event->tick));
    PARSE_TRY(read_u32(&fields[4], false, &event->duration_ticks));
    return read_i32(&fields[5], &event->detail);
}

/* Leer ACK/NACK conservando el tipo y request_id de la solicitud original */
static MessageParseResult read_reply(const JSONPair_t *data, Message *out)
{
    MessageField fields[] = {{"message_type", {0}}, {"reason", {0}}};
    size_t count = out->type == MSG_ACK ? 1u : 2u;
    uint32_t reason;

    PARSE_TRY(read_object(data->value, data->valueLength, fields, count));
    PARSE_TRY(read_message_type(&fields[0], &out->data.reply.type));

    if (out->type == MSG_ACK)
        return MESSAGE_PARSE_OK;

    PARSE_TRY(read_u32(&fields[1], true, &reason));

    if (reason > REJECT_NO_TASKS)
        return MESSAGE_PARSE_OUT_OF_RANGE;

    out->data.reply.reason = (RejectReason)reason;
    return MESSAGE_PARSE_OK;
}

/* Seleccionar el esquema de data a partir del tipo del mensaje */
static MessageParseResult read_data(const JSONPair_t *data, Message *out)
{
    if (data->jsonType != JSONObject)
        return MESSAGE_PARSE_INVALID_SCHEMA;

    switch (out->type) {
        case MSG_CONFIG:
            return read_config(data, out);

        case MSG_CALIBRATE_REQUEST:
        case MSG_CALIBRATE_RESPONSE:
        case MSG_TRIGGER_SPORADIC: {
            MessageField field = {"task_id", {0}};

            if (out->type == MSG_CALIBRATE_REQUEST)
                field.name = "sample_ticks";
            else if (out->type == MSG_CALIBRATE_RESPONSE)
                field.name = "ut_per_tick";

            PARSE_TRY(read_object(data->value, data->valueLength, &field, 1u));

            if (out->type == MSG_CALIBRATE_REQUEST)
                return read_u32(&field, true, &out->data.sample_ticks);
            if (out->type == MSG_CALIBRATE_RESPONSE)
                return read_u32(&field, true, &out->data.ut_per_tick);
            return read_u32(&field, false, &out->data.task_id);
        }

        case MSG_DEPLOY_TASK: {
            MessageField fields[] = {{"task", {0}}, {"cost_ticks", {0}}};

            PARSE_TRY(read_object(data->value, data->valueLength,
                                  fields, FIELD_COUNT(fields)));
            PARSE_TRY(read_task(&fields[0], &out->data.deploy_task.task));
            return read_u32(&fields[1], true, &out->data.deploy_task.cost_ticks);
        }

        case MSG_START:
        case MSG_STOP:
            return read_object(data->value, data->valueLength, NULL, 0u);

        case MSG_EVENT:
            return read_event(data, out);

        case MSG_TICK_REPORT: {
            MessageField fields[] = {{"tick", {0}}, {"task_id", {0}}};

            PARSE_TRY(read_object(data->value, data->valueLength,
                                  fields, FIELD_COUNT(fields)));
            PARSE_TRY(read_unsigned(&fields[0], UINT64_MAX,
                                    &out->data.tick_report.tick));
            return read_u32(&fields[1], false, &out->data.tick_report.task_id);
        }

        case MSG_ACK:
        case MSG_NACK:
            return read_reply(data, out);

        default:
            return MESSAGE_PARSE_UNKNOWN_TYPE;
    }
}

// =================================================================================================
/*
 * Valida el JSON completo antes de interpretar sus campos.
 * La salida se actualiza solo después de comprobar todo el contenido.
 */
MessageParseResult message_parse(const char *text, size_t length, Message *out)
{
    MessageField fields[] = {
        {"type", {0}}, {"node_id", {0}}, {"request_id", {0}}, {"data", {0}}
    };
    Message message = {0};

    if (text == NULL || out == NULL || length == 0u)
        return MESSAGE_PARSE_INVALID_ARGUMENT;

    if (length > MESSAGE_MAX_BYTES)
        return MESSAGE_PARSE_TOO_LARGE;

    if (JSON_Validate(text, length) != JSONSuccess)
        return MESSAGE_PARSE_INVALID_JSON;

    PARSE_TRY(read_object(text, length, fields, FIELD_COUNT(fields)));
    PARSE_TRY(read_message_type(&fields[0], &message.type));
    PARSE_TRY(read_u32(&fields[1], false, &message.node_id));
    PARSE_TRY(read_u32(&fields[2], false, &message.request_id));
    PARSE_TRY(read_data(&fields[3].pair, &message));

    *out = message;
    return MESSAGE_PARSE_OK;
}

/* Descripción para diagnósticos del Host o del firmware */
const char *message_parse_result_string(MessageParseResult result)
{
    switch (result) {
        case MESSAGE_PARSE_OK: return "Mensaje válido";
        case MESSAGE_PARSE_INVALID_ARGUMENT: return "Argumento inválido";
        case MESSAGE_PARSE_TOO_LARGE: return "Mensaje demasiado largo";
        case MESSAGE_PARSE_INVALID_JSON: return "JSON inválido o incompleto";
        case MESSAGE_PARSE_INVALID_SCHEMA: return "Campos o tipos inválidos";
        case MESSAGE_PARSE_OUT_OF_RANGE: return "Valor fuera de rango";
        case MESSAGE_PARSE_UNKNOWN_TYPE: return "Tipo de mensaje desconocido";
        default: return "Resultado desconocido";
    }
}
