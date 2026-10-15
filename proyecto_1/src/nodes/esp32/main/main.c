#include <string.h>

#include "driver/uart.h"
#include "esp_log.h"

#define UART_PORT   UART_NUM_0
#define BAUD_RATE   115200
#define BUFFER_SIZE 128

void app_main(void)
{
    // UART0 is commonly connected to the DevKit USB-UART bridge.
    // GPIO pins are left at their existing UART assignments.
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

    char line[BUFFER_SIZE];
    size_t used = 0;
    uint8_t ch;

    while (1) {
        int n = uart_read_bytes(
            UART_PORT, &ch, 1, pdMS_TO_TICKS(100)
        );

        if (n <= 0)
            continue;

        if (ch == '\r')
            continue;

        if (ch == '\n') {
            line[used] = '\0';

            if (strcmp(line, "PING") == 0) {
                const char *response = "PONG from ESP32\n";
                uart_write_bytes(
                    UART_PORT, response, strlen(response)
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
