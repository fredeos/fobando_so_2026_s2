#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "common/events.h"
#include "common/protocol.h"
#include "common/task.h"

int main(void)
{
    /*
     * Prueba 1: CRC-16/CCITT-FALSE.
     * Valor conocido para "123456789": 0x29B1.
     */
    const uint8_t sample[] = "123456789";

    assert(
        protocol_crc16(
            sample,
            sizeof(sample) - 1u
        ) == 0x29B1u
    );

    /*
     * Prueba 2: representación de una tarea.
     */
    Task task = {
        .id = 1u,
        .type = TASK_PERIODIC,
        .work = 40u,
        .period = 20u,
        .deadline = 20u,
        .criticality = 2
    };

    assert(task.id == 1u);
    assert(task.type == TASK_PERIODIC);
    assert(task.work == 40u);

    /*
     * Prueba 3: representación de un evento.
     */
    Event event = {
        .type = EVENT_TASK_ACCEPTED,
        .source = EVENT_SOURCE_HOST,
        .node_id = 1u,
        .task_id = task.id,
        .cost_ticks = 2u,
        .task = task
    };

    assert(event.task_id == task.id);
    assert(event.task.work == 40u);
    assert(event.cost_ticks == 2u);

    printf("OK: foundation tests\n");

    return 0;
}