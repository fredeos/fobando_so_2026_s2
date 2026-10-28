#include "calibrator.h"

#include <stddef.h>
#include <stdint.h>

#include "pico/stdlib.h"
#include "common/ut.h"

// -----------------------------------------------------------------------------------
// calibrator_run
// -----------------------------------------------------------------------------------

bool calibrator_run(
    uint32_t tick_us,
    uint32_t reserve_percent,
    CalibrationResult *result
) {
    if (result == NULL ||
        tick_us == 0 ||
        reserve_percent >= 100) {
        return false;
    }

    result->tick_us = tick_us;
    result->units_per_tick = 0;

    // -------------------------------------------------------------------------------
    // Calculate available execution budget
    // -------------------------------------------------------------------------------

    uint64_t budget_us =
        ((uint64_t)tick_us * (100u - reserve_percent)) / 100u;

    if (budget_us == 0) {
        return false;
    }

    // -------------------------------------------------------------------------------
    // Measure executable units of work
    // -------------------------------------------------------------------------------

    uint64_t start_us = time_us_64();
    uint64_t elapsed_us = 0;
    uint32_t units_executed = 0;

    while (elapsed_us < budget_us) {
        unidad_trabajo();
        units_executed++;

        elapsed_us = time_us_64() - start_us;
    }

    // -------------------------------------------------------------------------------
    // Store calibration result
    // -------------------------------------------------------------------------------

    result->units_per_tick = units_executed;

    return true;
}