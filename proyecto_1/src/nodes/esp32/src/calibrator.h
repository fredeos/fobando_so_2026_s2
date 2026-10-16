#ifndef CALIBRATOR_H
#define CALIBRATOR_H

#include <stdbool.h>
#include <stdint.h>

// -----------------------------------------------------------------------------------
// Calibration structures
// -----------------------------------------------------------------------------------

typedef struct {
    uint32_t tick_us;
    uint32_t units_per_tick;
} CalibrationResult;

// -----------------------------------------------------------------------------------
// Calibration functions
// -----------------------------------------------------------------------------------

bool calibrator_run(
    uint32_t tick_us,
    uint32_t reserve_percent,
    CalibrationResult *result
);

#endif