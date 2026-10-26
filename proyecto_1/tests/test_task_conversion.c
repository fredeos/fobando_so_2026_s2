#include <assert.h>
#include <limits.h>
#include <stdio.h>

#include "task_conversion.h"

int main(void)
{
    StoredTask stored = {
        .task = {
            .id = 12u,
            .type = TASK_SPORADIC,
            .work = 480u,
            .period = 50u,
            .deadline = 45u,
            .criticality = 3
        },
        .cost_ticks = 7u
    };
    SchedulerTask converted = {0};

    assert(scheduler_task_from_stored(&stored, &converted));
    assert(converted.id == 12u);
    assert(converted.type == 'S');
    assert(converted.W == 480u);
    assert(converted.T == 50u);
    assert(converted.D == 45u);
    assert(converted.criticality == 3u);
    assert(converted.C == 7u);

    stored.task.criticality = -1;
    SchedulerTask unchanged = {
        .id = 99u, .type = 'P', .W = 8u, .T = 9u, .D = 10u,
        .criticality = 11u, .C = 12u
    };
    assert(!scheduler_task_from_stored(&stored, &unchanged));
    assert(unchanged.id == 99u && unchanged.W == 8u && unchanged.C == 12u);

    stored.task.criticality = INT_MAX;
    stored.cost_ticks = 0u;
    assert(!scheduler_task_from_stored(&stored, &converted));
    assert(!scheduler_task_from_stored(NULL, &converted));
    assert(!scheduler_task_from_stored(&stored, NULL));

    puts("OK: scheduler task conversion tests");
    return 0;
}
