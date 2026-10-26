#include "common/message_parser.h"

#include <stdbool.h>
#include <inttypes.h>
#include <stdio.h>

/* Mantiene los nombres JSON estables aunque cambien los valores internos del enum. */
static const char *message_type_name(MessageType type)
{
    switch (type) {
        case MSG_CONFIG: return "CONFIG";
        case MSG_CALIBRATE_REQUEST: return "CALIBRATE_REQUEST";
        case MSG_CALIBRATE_RESPONSE: return "CALIBRATE_RESPONSE";
        case MSG_DEPLOY_TASK: return "DEPLOY_TASK";
        case MSG_START: return "START";
        case MSG_EVENT: return "EVENT";
        case MSG_TICK_REPORT: return "TICK_REPORT";
        case MSG_TRIGGER_SPORADIC: return "TRIGGER_SPORADIC";
        case MSG_STOP: return "STOP";
        case MSG_ACK: return "ACK";
        case MSG_NACK: return "NACK";
        default: return NULL;
    }
}

static const char *algorithm_name(MessageAlgorithm algorithm)
{
    switch (algorithm) {
        case MESSAGE_SCHED_RMS: return "RMS";
        case MESSAGE_SCHED_EDF: return "EDF";
        default: return NULL;
    }
}

static bool format_data(const Message *message, char *data, size_t capacity)
{
    int written;

    switch (message->type) {
        case MSG_CONFIG: {
            const char *algorithm = algorithm_name(message->data.config.algorithm);
            if (algorithm == NULL ||
                message->data.config.tick_ms == 0u ||
                message->data.config.report_every_ticks == 0u) {
                return false;
            }
            written = snprintf(data, capacity,
                "{\"algorithm\":\"%s\",\"tick_ms\":%" PRIu32
                ",\"report_every_ticks\":%" PRIu32 "}",
                algorithm, message->data.config.tick_ms,
                message->data.config.report_every_ticks);
            break;
        }
        case MSG_CALIBRATE_REQUEST:
            if (message->data.sample_ticks == 0u)
                return false;
            written = snprintf(data, capacity, "{\"sample_ticks\":%" PRIu32 "}",
                message->data.sample_ticks);
            break;
        case MSG_CALIBRATE_RESPONSE:
            if (message->data.ut_per_tick == 0u)
                return false;
            written = snprintf(data, capacity, "{\"ut_per_tick\":%" PRIu32 "}",
                message->data.ut_per_tick);
            break;
        case MSG_DEPLOY_TASK: {
            const Task *task = &message->data.deploy_task.task;
            if ((task->type != TASK_PERIODIC && task->type != TASK_SPORADIC) ||
                task->work == 0u || task->period == 0u || task->deadline == 0u ||
                message->data.deploy_task.cost_ticks == 0u) {
                return false;
            }
            written = snprintf(data, capacity,
                "{\"task\":{\"id\":%" PRIu32 ",\"type\":\"%c\","
                "\"work\":%" PRIu32 ",\"period\":%" PRIu32
                ",\"deadline\":%" PRIu32 ",\"criticality\":%" PRId32
                "},\"cost_ticks\":%" PRIu32 "}",
                task->id, (char)task->type, task->work, task->period,
                task->deadline, task->criticality,
                message->data.deploy_task.cost_ticks);
            break;
        }
        case MSG_START:
        case MSG_STOP:
            written = snprintf(data, capacity, "{}");
            break;
        case MSG_EVENT: {
            const Event *event = &message->data.event;
            if (event->type < EVENT_TASK_ACCEPTED || event->type > EVENT_TASK_SWITCH)
                return false;
            written = snprintf(data, capacity,
                "{\"event_type\":%u,\"task_id\":%" PRIu32
                ",\"job_id\":%" PRIu32 ",\"tick\":%" PRIu64
                ",\"duration_ticks\":%" PRIu32 ",\"detail\":%" PRId32 "}",
                (unsigned)event->type, event->task_id, event->job_id,
                event->tick, event->duration_ticks, event->detail);
            break;
        }
        case MSG_TICK_REPORT:
            written = snprintf(data, capacity, "{\"tick\":%" PRIu64
                ",\"task_id\":%" PRIu32 "}",
                message->data.tick_report.tick, message->data.tick_report.task_id);
            break;
        case MSG_TRIGGER_SPORADIC:
            written = snprintf(data, capacity, "{\"task_id\":%" PRIu32 "}",
                message->data.task_id);
            break;
        case MSG_ACK:
        case MSG_NACK: {
            const char *reply_type = message_type_name(message->data.reply.type);
            if (reply_type == NULL)
                return false;
            if (message->type == MSG_ACK) {
                written = snprintf(data, capacity,
                    "{\"message_type\":\"%s\"}", reply_type);
            } else {
                if (message->data.reply.reason <= REJECT_NONE ||
                    message->data.reply.reason > REJECT_CALIBRATION_FAILED) {
                    return false;
                }
                written = snprintf(data, capacity,
                    "{\"message_type\":\"%s\",\"reason\":%u}",
                    reply_type, (unsigned)message->data.reply.reason);
            }
            break;
        }
        default:
            return false;
    }

    return written >= 0 && (size_t)written < capacity;
}

MessageSerializeResult message_serialize(
    const Message *message,
    char *buffer,
    size_t capacity,
    size_t *length
)
{
    char data[MESSAGE_MAX_BYTES + 1u];
    char serialized[MESSAGE_MAX_BYTES + 1u];
    const char *type;

    if (message == NULL || buffer == NULL || length == NULL || capacity == 0u)
        return MESSAGE_SERIALIZE_INVALID_ARGUMENT;

    type = message_type_name(message->type);
    if (type == NULL || !format_data(message, data, sizeof(data)))
        return MESSAGE_SERIALIZE_INVALID_MESSAGE;

    /* Se construye en temporal para no modificar el buffer del llamador si falla. */
    int written = snprintf(serialized, sizeof(serialized),
        "{\"type\":\"%s\",\"node_id\":%" PRIu32
        ",\"request_id\":%" PRIu32 ",\"data\":%s}",
        type, message->node_id, message->request_id, data);
    if (written < 0 || (size_t)written > MESSAGE_MAX_BYTES ||
        (size_t)written >= capacity) {
        return MESSAGE_SERIALIZE_TOO_LARGE;
    }

    for (size_t i = 0u; i <= (size_t)written; ++i)
        buffer[i] = serialized[i];
    *length = (size_t)written;
    return MESSAGE_SERIALIZE_OK;
}
