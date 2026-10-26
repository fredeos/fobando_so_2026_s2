#include "task_conversion.h"

bool scheduler_task_from_stored(
    const StoredTask *stored,
    SchedulerTask *out
)
{
    if (stored == NULL || out == NULL)
        return false;

    const Task *source = &stored->task;
    if ((source->type != TASK_PERIODIC && source->type != TASK_SPORADIC) ||
        source->work == 0u || source->period == 0u ||
        source->deadline == 0u || source->criticality < 0 ||
        stored->cost_ticks == 0u) {
        return false;
    }

    SchedulerTask converted = {
        .id = source->id,
        .type = (char)source->type,
        .W = source->work,
        .T = source->period,
        .D = source->deadline,
        .criticality = (uint32_t)source->criticality,
        .C = stored->cost_ticks
    };
    *out = converted;
    return true;
}
