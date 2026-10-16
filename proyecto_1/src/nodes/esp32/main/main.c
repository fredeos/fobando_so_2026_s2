#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "driver/uart.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "calibrator.h"

// -----------------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------------

#define UART_PORT   UART_NUM_0
#define BAUD_RATE   115200
#define BUFFER_SIZE 128

#define TEST_TICK_US 10000u
#define TEST_RESERVE_PERCENT 20u
#define TEST_COUNT 5u

static const char *TAG = "ESP32_NODE";

// -----------------------------------------------------------------------------------
// run_calibration_test
// -----------------------------------------------------------------------------------

static void run_calibration_test(void)
{
    printf("\n");
    printf("----------------------------------------\n");
    printf(" Calibration Test - ESP32\n");
    printf("----------------------------------------\n");

    printf(
        "Tick duration: %" PRIu32 " us\n",
        (uint32_t)TEST_TICK_US
    );

    printf(
        "System reserve: %" PRIu32 " %%\n",
        (uint32_t)TEST_RESERVE_PERCENT
    );

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
                (uint32_t)(i + 1),
                (uint32_t)result.units_per_tick
            );
        } else {
            printf(
                "Test %" PRIu32 ": Calibration failed\n",
                (uint32_t)(i + 1)
            );
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }

    printf("----------------------------------------\n");
    printf("Calibration test finished.\n");
}

// -----------------------------------------------------------------------------------
// app_main
// -----------------------------------------------------------------------------------

void app_main(void)
{
    // Run calibration.

    run_calibration_test();
    // Configure UART.

    const uart_config_t config = {
        .baud_rate = BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT
    };

    ESP_ERROR_CHECK(
        uart_driver_install(UART_PORT, 1024, 0, 0, NULL, 0)
    );

    ESP_ERROR_CHECK(
        uart_param_config(UART_PORT, &config)
    );

    // Keep application logs off the serial protocol channel.

    esp_log_level_set("*", ESP_LOG_NONE);
    esp_log_level_set(TAG, ESP_LOG_NONE);

    // UART command processing.

    char line[BUFFER_SIZE];
    size_t used = 0;
    uint8_t ch;

    while (1) {
        int n = uart_read_bytes(
            UART_PORT,
            &ch,
            1,
            pdMS_TO_TICKS(100)
        );

        if (n <= 0) {
            continue;
        }

        if (ch == '\r') {
            continue;
        }

        if (ch == '\n') {
            line[used] = '\0';

            if (strcmp(line, "PING") == 0) {
                const char *response = "PONG from ESP32\n";

                uart_write_bytes(
                    UART_PORT,
                    response,
                    strlen(response)
                );
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