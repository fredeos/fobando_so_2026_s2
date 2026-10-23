
/*

#include <stdio.h>
#include <inttypes.h>

#include "pico/stdlib.h"
#include "calibrator.h"

// -----------------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------------

#define TEST_TICK_US 10000u
#define TEST_RESERVE_PERCENT 20u
#define TEST_COUNT 5u

// -----------------------------------------------------------------------------------
// main
// -----------------------------------------------------------------------------------

int main(void) {
    stdio_init_all();

    printf("\n");
    printf("----------------------------------------\n");
    printf(" Calibration Test - Raspberry Pi Pico\n");
    printf("----------------------------------------\n");

    printf("Tick duration: %" PRIu32 " us\n", TEST_TICK_US);
    printf("System reserve: %" PRIu32 " %%\n", TEST_RESERVE_PERCENT);

    for (uint32_t i = 0; i < TEST_COUNT; i++) {
        CalibrationResult result;

        bool success = calibrator_run(
            TEST_TICK_US,
            TEST_RESERVE_PERCENT,
            &result
        );

        if (success) {
            printf(
                "Test %" PRIu32 ": %" PRIu32 " UT/tick\n",
                i + 1,
                result.units_per_tick
            );
        } else {
            printf("Test %" PRIu32 ": Calibration failed\n", i + 1);
        }
    }

    printf("----------------------------------------\n");
    printf("Calibration test finished.\n");

    while (true) {
        tight_loop_contents();
    }

    return 0;
}

*/

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"

#define BUFFER_SIZE 128

int main(void)
{
    stdio_init_all();

    // Wait for the computer to connect to the USB serial port.
    while (!stdio_usb_connected()) {
        sleep_ms(100);
    }

    char line[BUFFER_SIZE];
    size_t used = 0;

    while (1) {
        int ch = getchar_timeout_us(0);

        if (ch == PICO_ERROR_TIMEOUT) {
            sleep_ms(1);
            continue;
        }

        if (ch == '\r')
            continue;

        if (ch == '\n') {
            line[used] = '\0';

            if (strcmp(line, "PING") == 0) {
                printf("PONG from Pico\n");
                fflush(stdout);
            }

            used = 0;
        } else if (used < sizeof(line) - 1) {
            line[used++] = (char)ch;
        } else {
            // Discard an oversized line.
            used = 0;
        }
    }
}