#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "common/message_parser.h"

/* Construir ejemplos completos para comprobar el contrato público */
static Message parse(const char *type, const char *data)
{
    char text[MESSAGE_MAX_BYTES + 1u];
    int length = snprintf(text, sizeof(text),
        "{\"type\":\"%s\",\"node_id\":2,\"request_id\":7,\"data\":%s}",
        type, data);
    Message message;

    assert(length > 0 && (size_t)length < sizeof(text));
    assert(message_parse(text, (size_t)length, &message) == MESSAGE_PARSE_OK);
    assert(message.node_id == 2u && message.request_id == 7u);
    return message;
}

/* Un rechazo debe conservar todos los bytes de la salida del llamador */
static void reject_bytes(
    const char *text,
    size_t length,
    MessageParseResult expected
)
{
    Message message;
    unsigned char original[sizeof(message)];

    memset(&message, 0xa5, sizeof(message));
    memcpy(original, &message, sizeof(message));
    assert(message_parse(text, length, &message) == expected);
    assert(memcmp(original, &message, sizeof(message)) == 0);
}

static void reject(const char *text, MessageParseResult expected)
{
    reject_bytes(text, strlen(text), expected);
}

/*
 * Prueba 1:
 * Interpretar todos los tipos y reutilizar Task, Event y RejectReason.
 */
static void test_message_types(void)
{
    Message message = parse("CONFIG",
        "{\"algorithm\":\"RMS\",\"tick_ms\":10,\"report_every_ticks\":20}");
    assert(message.type == MSG_CONFIG);
    assert(message.data.config.algorithm == MESSAGE_SCHED_RMS);
    assert(message.data.config.tick_ms == 10u);
    assert(message.data.config.report_every_ticks == 20u);

    message = parse("CONFIG",
        "{\"report_every_ticks\":1,\"tick_ms\":1,\"algorithm\":\"EDF\"}");
    assert(message.data.config.algorithm == MESSAGE_SCHED_EDF);

    message = parse("CALIBRATE_REQUEST", "{\"sample_ticks\":50}");
    assert(message.type == MSG_CALIBRATE_REQUEST);
    assert(message.data.sample_ticks == 50u);

    message = parse("CALIBRATE_RESPONSE", "{\"ut_per_tick\":100}");
    assert(message.type == MSG_CALIBRATE_RESPONSE);
    assert(message.data.ut_per_tick == 100u);

    message = parse("DEPLOY_TASK",
        "{\"cost_ticks\":3,\"task\":{\"id\":8,\"type\":\"P\",\"work\":40,"
        "\"period\":20,\"deadline\":15,\"criticality\":-1}}");
    assert(message.type == MSG_DEPLOY_TASK);
    assert(message.data.deploy_task.cost_ticks == 3u);
    assert(message.data.deploy_task.task.id == 8u);
    assert(message.data.deploy_task.task.type == TASK_PERIODIC);
    assert(message.data.deploy_task.task.work == 40u);
    assert(message.data.deploy_task.task.period == 20u);
    assert(message.data.deploy_task.task.deadline == 15u);
    assert(message.data.deploy_task.task.criticality == -1);

    message = parse("START", "{}");
    assert(message.type == MSG_START);
    message = parse("STOP", "{}");
    assert(message.type == MSG_STOP);

    message = parse("EVENT",
        "{\"event_type\":5,\"task_id\":8,\"job_id\":3,\"tick\":99,"
        "\"duration_ticks\":2,\"detail\":-7}");
    assert(message.type == MSG_EVENT);
    assert(message.data.event.type == EVENT_EXECUTION_SLICE);
    assert(message.data.event.source == EVENT_SOURCE_NODE);
    assert(message.data.event.node_id == 2u);
    assert(message.data.event.task_id == 8u);
    assert(message.data.event.job_id == 3u);
    assert(message.data.event.tick == 99u);
    assert(message.data.event.duration_ticks == 2u);
    assert(message.data.event.detail == -7);
    assert(message.data.event.host_time_ns == 0u);
    assert(message.data.event.cost_ticks == 0u);
    assert(message.data.event.ut_per_tick == 0u);
    assert(message.data.event.task.work == 0u);

    message = parse("TICK_REPORT", "{\"tick\":9007199254740993,\"task_id\":8}");
    assert(message.type == MSG_TICK_REPORT);
    assert(message.data.tick_report.tick == UINT64_C(9007199254740993));
    assert(message.data.tick_report.task_id == 8u);

    message = parse("TRIGGER_SPORADIC", "{\"task_id\":8}");
    assert(message.type == MSG_TRIGGER_SPORADIC);
    assert(message.data.task_id == 8u);

    message = parse("ACK", "{\"message_type\":\"DEPLOY_TASK\"}");
    assert(message.type == MSG_ACK);
    assert(message.data.reply.type == MSG_DEPLOY_TASK);
    assert(message.data.reply.reason == REJECT_NONE);

    message = parse("NACK", "{\"message_type\":\"DEPLOY_TASK\",\"reason\":3}");
    assert(message.type == MSG_NACK);
    assert(message.data.reply.type == MSG_DEPLOY_TASK);
    assert(message.data.reply.reason == REJECT_INSUFFICIENT_CAPACITY);
}

/*
 * Prueba 2:
 * Respetar la longitud exacta, los buffers sin NUL y el límite de bytes.
 */
static void test_input_boundaries(void)
{
    const char text[] =
        "{\"type\":\"START\",\"node_id\":0,\"request_id\":0,\"data\":{}}";
    char exact[sizeof(text) - 1u];
    char copy[sizeof(exact)];
    Message message;

    memcpy(exact, text, sizeof(exact));
    memcpy(copy, exact, sizeof(copy));
    assert(message_parse(exact, sizeof(exact), &message) == MESSAGE_PARSE_OK);
    assert(memcmp(exact, copy, sizeof(exact)) == 0);

    for (size_t i = 1u; i < sizeof(exact); ++i)
        reject_bytes(exact, i, MESSAGE_PARSE_INVALID_JSON);

    reject_bytes(NULL, 10u, MESSAGE_PARSE_INVALID_ARGUMENT);
    reject_bytes(text, 0u, MESSAGE_PARSE_INVALID_ARGUMENT);
    assert(message_parse(text, sizeof(text) - 1u, NULL)
           == MESSAGE_PARSE_INVALID_ARGUMENT);
    reject_bytes(text, sizeof(text), MESSAGE_PARSE_INVALID_JSON);

    char padded[MESSAGE_MAX_BYTES + 1u];
    memset(padded, ' ', sizeof(padded));
    memcpy(padded, exact, sizeof(exact));
    assert(message_parse(padded, MESSAGE_MAX_BYTES, &message) == MESSAGE_PARSE_OK);
    reject_bytes(padded, sizeof(padded), MESSAGE_PARSE_TOO_LARGE);

    /* Los bytes posteriores a length pertenecen al llamador, no al JSON */
    padded[sizeof(exact)] = 'X';
    assert(message_parse(padded, sizeof(exact), &message) == MESSAGE_PARSE_OK);
    reject_bytes(padded, sizeof(exact) + 1u, MESSAGE_PARSE_INVALID_JSON);

    memcpy(padded, exact, sizeof(exact));
    padded[10] = '\0';
    reject_bytes(padded, sizeof(exact), MESSAGE_PARSE_INVALID_JSON);

    /* Espacios iniciales, campos reordenados y saltos de línea */
    const char reordered[] =
        " \r\n{\"data\":{},\"request_id\":7,\"node_id\":2,\"type\":\"STOP\"}\t";
    assert(message_parse(reordered, sizeof(reordered) - 1u, &message)
           == MESSAGE_PARSE_OK);
    assert(message.type == MSG_STOP);
}

/*
 * Prueba 3:
 * Rechazar JSON roto, datos ambiguos y campos que no pertenecen al tipo.
 */
static void test_invalid_messages(void)
{
    const char *bad_json[] = {
        "", "{", "{}{}", "{\"type\":\"START\",}", "{type:1}",
        "{\"type\":\"bad\\q\"}", "{\"number\":01}", "{\"number\":NaN}"
    };

    for (size_t i = 1u; i < sizeof(bad_json) / sizeof(bad_json[0]); ++i)
        reject(bad_json[i], MESSAGE_PARSE_INVALID_JSON);

    const char *bad_schema[] = {
        "{}", "[]", "null", "42", "\"START\"",
        "{\"type\":\"START\",\"node_id\":1,\"request_id\":1}",
        "{\"type\":\"START\",\"node_id\":1,\"request_id\":1,\"data\":[]}",
        "{\"type\":\"START\",\"node_id\":1,\"request_id\":1,\"data\":{},\"x\":0}",
        "{\"type\":\"START\",\"type\":\"STOP\",\"node_id\":1,\"request_id\":1,\"data\":{}}",
        "{\"type\":\"START\",\"node_id\":1,\"node_id\":2,\"request_id\":1,\"data\":{}}",
        "{\"t\\u0079pe\":\"START\",\"node_id\":1,\"request_id\":1,\"data\":{}}",
        "{\"type\":1,\"node_id\":1,\"request_id\":1,\"data\":{}}",
        "{\"type\":\"START\",\"node_id\":\"1\",\"request_id\":1,\"data\":{}}",
        "{\"type\":\"START\",\"node_id\":-1,\"request_id\":1,\"data\":{}}",
        "{\"type\":\"START\",\"node_id\":1.0,\"request_id\":1,\"data\":{}}",
        "{\"type\":\"START\",\"node_id\":1e0,\"request_id\":1,\"data\":{}}",
        "{\"type\":\"STOP\",\"node_id\":1,\"request_id\":1,\"data\":{\"task_id\":1}}",
        "{\"type\":\"CALIBRATE_RESPONSE\",\"node_id\":1,\"request_id\":1,\"data\":{\"ut_per_tick\":true}}",
        "{\"type\":\"CALIBRATE_REQUEST\",\"node_id\":1,\"request_id\":1,\"data\":{\"sample_ticks\":1,\"sample_ticks\":2}}",
        "{\"type\":\"CONFIG\",\"node_id\":1,\"request_id\":1,\"data\":{\"algorithm\":\"FIFO\",\"tick_ms\":1,\"report_every_ticks\":1}}",
        "{\"type\":\"ACK\",\"node_id\":1,\"request_id\":1,\"data\":{\"message_type\":\"START\",\"reason\":0}}",
        "{\"type\":\"DEPLOY_TASK\",\"node_id\":1,\"request_id\":1,\"data\":{\"task\":{},\"cost_ticks\":1}}",
        "{\"type\":\"DEPLOY_TASK\",\"node_id\":1,\"request_id\":1,\"data\":{\"task\":{\"id\":1,\"type\":\"X\",\"work\":1,\"period\":1,\"deadline\":1,\"criticality\":0},\"cost_ticks\":1}}"
    };

    for (size_t i = 0u; i < sizeof(bad_schema) / sizeof(bad_schema[0]); ++i)
        reject(bad_schema[i], MESSAGE_PARSE_INVALID_SCHEMA);

    reject("{\"type\":\"PING\",\"node_id\":1,\"request_id\":1,\"data\":{}}",
        MESSAGE_PARSE_UNKNOWN_TYPE);
    reject("{\"type\":\"ACK\",\"node_id\":1,\"request_id\":1,\"data\":{\"message_type\":\"PING\"}}",
        MESSAGE_PARSE_UNKNOWN_TYPE);
}

/*
 * Prueba 4:
 * Preservar enteros de 64 bits y comprobar desbordamientos y rangos.
 */
static void test_numeric_limits(void)
{
    Message message = parse("TICK_REPORT",
        "{\"tick\":18446744073709551615,\"task_id\":4294967295}");
    assert(message.data.tick_report.tick == UINT64_MAX);
    assert(message.data.tick_report.task_id == UINT32_MAX);

    message = parse("DEPLOY_TASK",
        "{\"task\":{\"id\":0,\"type\":\"S\",\"work\":4294967295,\"period\":1,"
        "\"deadline\":1,\"criticality\":-2147483648},\"cost_ticks\":1}");
    assert(message.data.deploy_task.task.type == TASK_SPORADIC);
    assert(message.data.deploy_task.task.work == UINT32_MAX);
    assert(message.data.deploy_task.task.criticality == INT32_MIN);

    message = parse("EVENT",
        "{\"event_type\":15,\"task_id\":0,\"job_id\":0,\"tick\":0,"
        "\"duration_ticks\":0,\"detail\":2147483647}");
    assert(message.data.event.type == EVENT_TASK_SWITCH);
    assert(message.data.event.detail == INT32_MAX);

    const char *out_of_range[] = {
        "{\"type\":\"START\",\"node_id\":4294967296,\"request_id\":1,\"data\":{}}",
        "{\"type\":\"START\",\"node_id\":1,\"request_id\":4294967296,\"data\":{}}",
        "{\"type\":\"CALIBRATE_RESPONSE\",\"node_id\":1,\"request_id\":1,\"data\":{\"ut_per_tick\":0}}",
        "{\"type\":\"CALIBRATE_REQUEST\",\"node_id\":1,\"request_id\":1,\"data\":{\"sample_ticks\":0}}",
        "{\"type\":\"TICK_REPORT\",\"node_id\":1,\"request_id\":1,\"data\":{\"tick\":18446744073709551616,\"task_id\":0}}",
        "{\"type\":\"EVENT\",\"node_id\":1,\"request_id\":1,\"data\":{\"event_type\":16,\"task_id\":0,\"job_id\":0,\"tick\":0,\"duration_ticks\":0,\"detail\":0}}",
        "{\"type\":\"EVENT\",\"node_id\":1,\"request_id\":1,\"data\":{\"event_type\":1,\"task_id\":0,\"job_id\":0,\"tick\":0,\"duration_ticks\":0,\"detail\":2147483648}}",
        "{\"type\":\"EVENT\",\"node_id\":1,\"request_id\":1,\"data\":{\"event_type\":1,\"task_id\":0,\"job_id\":0,\"tick\":0,\"duration_ticks\":0,\"detail\":-2147483649}}",
        "{\"type\":\"NACK\",\"node_id\":1,\"request_id\":1,\"data\":{\"message_type\":\"START\",\"reason\":0}}",
        "{\"type\":\"NACK\",\"node_id\":1,\"request_id\":1,\"data\":{\"message_type\":\"START\",\"reason\":7}}",
        "{\"type\":\"DEPLOY_TASK\",\"node_id\":1,\"request_id\":1,\"data\":{\"task\":{\"id\":1,\"type\":\"P\",\"work\":0,\"period\":1,\"deadline\":1,\"criticality\":0},\"cost_ticks\":1}}"
    };

    for (size_t i = 0u; i < sizeof(out_of_range) / sizeof(out_of_range[0]); ++i)
        reject(out_of_range[i], MESSAGE_PARSE_OUT_OF_RANGE);
}

static void assert_round_trip(const Message *source)
{
    char json[MESSAGE_MAX_BYTES + 1u];
    size_t length = 0u;
    Message parsed;

    assert(message_serialize(source, json, sizeof(json), &length)
           == MESSAGE_SERIALIZE_OK);
    assert(length <= MESSAGE_MAX_BYTES);
    assert(message_parse(json, length, &parsed) == MESSAGE_PARSE_OK);
    assert(parsed.type == source->type);
    assert(parsed.node_id == source->node_id);
    assert(parsed.request_id == source->request_id);
}

static void test_message_serialization(void)
{
    Message message = {
        .type = MSG_CONFIG, .node_id = 2u, .request_id = 7u,
        .data.config = {
            .algorithm = MESSAGE_SCHED_EDF,
            .tick_ms = 10u,
            .report_every_ticks = 20u
        }
    };
    assert_round_trip(&message);

    message = (Message){
        .type = MSG_CALIBRATE_REQUEST, .node_id = 2u, .request_id = 8u,
        .data.sample_ticks = 50u
    };
    assert_round_trip(&message);
    message.type = MSG_CALIBRATE_RESPONSE;
    message.data.ut_per_tick = 100u;
    assert_round_trip(&message);

    message = (Message){
        .type = MSG_DEPLOY_TASK, .node_id = 2u, .request_id = 9u,
        .data.deploy_task = {
            .task = {.id = 8u, .type = TASK_SPORADIC, .work = 40u,
                .period = 20u, .deadline = 15u, .criticality = -1},
            .cost_ticks = 3u
        }
    };
    assert_round_trip(&message);

    message = (Message){.type = MSG_START, .node_id = 2u, .request_id = 10u};
    assert_round_trip(&message);
    message.type = MSG_STOP;
    assert_round_trip(&message);

    message = (Message){
        .type = MSG_EVENT, .node_id = 2u, .request_id = 11u,
        .data.event = {.type = EVENT_TASK_SWITCH, .task_id = 8u,
            .job_id = 3u, .tick = UINT64_C(9007199254740993),
            .duration_ticks = 2u, .detail = -7}
    };
    assert_round_trip(&message);

    message = (Message){
        .type = MSG_TICK_REPORT, .node_id = 2u, .request_id = 12u,
        .data.tick_report = {.tick = UINT64_MAX, .task_id = 8u}
    };
    assert_round_trip(&message);
    message = (Message){
        .type = MSG_TRIGGER_SPORADIC, .node_id = 2u, .request_id = 13u,
        .data.task_id = 8u
    };
    assert_round_trip(&message);

    message = (Message){
        .type = MSG_ACK, .node_id = 2u, .request_id = 14u,
        .data.reply = {.type = MSG_DEPLOY_TASK}
    };
    assert_round_trip(&message);
    message.type = MSG_NACK;
    message.data.reply.reason = REJECT_INSUFFICIENT_CAPACITY;
    assert_round_trip(&message);

    char buffer[MESSAGE_MAX_BYTES + 1u];
    size_t length = 123u;
    const Message start = {.type = MSG_START};
    memset(buffer, 0xA5, sizeof(buffer));
    assert(message_serialize(&start, buffer, 4u, &length)
           == MESSAGE_SERIALIZE_TOO_LARGE);
    assert(length == 123u && (unsigned char)buffer[0] == 0xA5u);
    assert(message_serialize(NULL, buffer, sizeof(buffer), &length)
           == MESSAGE_SERIALIZE_INVALID_ARGUMENT);
    assert(message_serialize(&start, NULL, sizeof(buffer), &length)
           == MESSAGE_SERIALIZE_INVALID_ARGUMENT);

    Message invalid = {.type = MSG_CALIBRATE_REQUEST};
    assert(message_serialize(&invalid, buffer, sizeof(buffer), &length)
           == MESSAGE_SERIALIZE_INVALID_MESSAGE);
}

int main(void)
{
    test_message_types();
    test_input_boundaries();
    test_invalid_messages();
    test_numeric_limits();
    test_message_serialization();

    for (int result = MESSAGE_PARSE_OK; result <= MESSAGE_PARSE_UNKNOWN_TYPE; ++result)
        assert(message_parse_result_string((MessageParseResult)result)[0] != '\0');

    puts("OK: message parser tests");
    return 0;
}
