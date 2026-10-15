#ifndef CALIBRATOR_H
#define CALIBRATOR_H

#include <stdint.h>
#include <stdbool.h>

// -----------------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------------

#define CALIBRATOR_BATCH_SIZE 100u
#define CALIBRATOR_SAMPLE_COUNT 5u

// -----------------------------------------------------------------------------------
// Data structures
// -----------------------------------------------------------------------------------

typedef struct {
    uint32_t tick_us;
    uint32_t units_per_tick;
} CalibrationResult;

// -----------------------------------------------------------------------------------
// Public functions
// -----------------------------------------------------------------------------------

bool calibrator_run(
    uint32_t tick_us,
    uint32_t reserve_percent,
    CalibrationResult *result
);

#endif