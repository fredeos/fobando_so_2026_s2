#include "calibrator.h"

#include <stddef.h>
#include <stdint.h>

#include "esp_timer.h"
#include "common/ut.h"

// -----------------------------------------------------------------------------------
// calibrator_run
// -----------------------------------------------------------------------------------

bool calibrator_run(
    uint32_t tick_us,
    uint32_t reserve_percent,
    CalibrationResult *result
) {
    if (result == NULL || tick_us == 0 || reserve_percent >= 100) {
        return false;
    }

    result->tick_us = tick_us;
    result->units_per_tick = 0;

    uint64_t budget_us =
        ((uint64_t)tick_us * (100u - reserve_percent)) / 100u;

    if (budget_us == 0) {
        return false;
    }

    uint64_t start_us = (uint64_t)esp_timer_get_time();
    uint64_t elapsed_us = 0;
    uint32_t units_executed = 0;

    while (elapsed_us < budget_us) {
        unidad_trabajo();

        units_executed++;

        elapsed_us =
            (uint64_t)esp_timer_get_time() - start_us;
    }

    result->units_per_tick = units_executed;

    return true;
}