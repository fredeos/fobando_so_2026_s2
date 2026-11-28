#include "common/json.h"
#include "common/protocol.h"
#include "common/screen_link_config.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gpio/gpio.h"
#include "serial/esp32.h"
#include "serial/serial.h"

#include <stdio.h>
#include <inttypes.h>
#include <string.h>

#define DEMO_TAG "screen_comm_demo"

/* Three external LEDs, each wired in series with a current-limiting resistor. */
#define LED_COMM_START 25u
#define LED_PACKET     26u
#define LED_TRANSFER_DONE 27u

/* UART0 is connected to the development board's USB-to-UART bridge. */
#define DEMO_UART UART_NUM_0
#define DEMO_UART_TX_PIN 1
#define DEMO_UART_RX_PIN 3
#define DEMO_UART_BAUDRATE SCREEN_LINK_BAUDRATE

#define DEMO_WIDTH 128u
#define DEMO_HEIGHT 160u
#define DEMO_FRAME_TIMEOUT_MS 10
#define DEMO_PULSE_MS 40u
#define DEMO_JSON_BUFFER PROTOCOL_PAYLOAD_BYTES

typedef struct {
    bool active;                 /* Hay una imagen en recepción. */
    uint32_t id;                  /* ID común de sus mensajes JSON. */
    uint32_t total_chunks;        /* Cantidad declarada por el host. */
    uint32_t next_chunk;          /* Índice que se espera recibir a continuación. */
    uint32_t channels;            /* 1 para GRAY8 o 3 para BGR888. */
    size_t received_pixel_bytes;  /* Bytes decodificados lógicamente desde hex. */
} demo_transfer_t;

static esp32_uart_t demo_uart;
static protocol_rx_t demo_rx;
static serial_packet_t demo_rx_packet;

/* Error fatal del demo: apaga inicio/final y deja el LED de paquete parpadeando. */
static void fail_with_led_pattern(void) {
    board_gpio_write(LED_COMM_START, GPIO_LOW);
    board_gpio_write(LED_TRANSFER_DONE, GPIO_LOW);
    for (;;) {
        board_gpio_write(LED_PACKET, GPIO_HIGH);
        vTaskDelay(pdMS_TO_TICKS(150));
        board_gpio_write(LED_PACKET, GPIO_LOW);
        vTaskDelay(pdMS_TO_TICKS(150));
    }
}

/* Configura LEDs, UART0 con los parámetros compartidos y la FSM de recepción. */
static int init_demo_hardware(void) {
    if (board_gpio_init(LED_COMM_START, GPIO_OUTPUT) != 0 ||
        board_gpio_init(LED_PACKET, GPIO_OUTPUT) != 0 ||
        board_gpio_init(LED_TRANSFER_DONE, GPIO_OUTPUT) != 0)
        return -1;

    board_gpio_write(LED_COMM_START, GPIO_LOW);
    board_gpio_write(LED_PACKET, GPIO_LOW);
    board_gpio_write(LED_TRANSFER_DONE, GPIO_LOW);

    esp32_uart_configs_t config = {
        .port_num = DEMO_UART,
        .baudrate = DEMO_UART_BAUDRATE,
        .rx_buffer_size = 1024,
        .tx_buffer_size = 1024,
        .tick_ms = DEMO_FRAME_TIMEOUT_MS,
    };
    if (serial_configure(&demo_uart, &config) != SERIAL_OK)
        return -1;
    if (uart_set_pin(DEMO_UART, DEMO_UART_TX_PIN, DEMO_UART_RX_PIN,
                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK)
        return -1;
    if (protocol_rx_init(&demo_rx, &demo_rx_packet) != PROTOCOL_OK)
        return -1;

    return 0;
}

/* Compara un campo JSON de tipo string sin asumir que tiene terminador NUL. */
static int json_string_is(const JsonField *field, const char *expected) {
    return field != NULL && expected != NULL && field->pair.jsonType == JSONString &&
           field->pair.value != NULL &&
           field->pair.valueLength == strlen(expected) &&
           memcmp(field->pair.value, expected, field->pair.valueLength) == 0;
}

/*
 * Extrae y valida los campos necesarios de un mensaje request/send. Además de
 * las dimensiones/formato, valida índice, total y que pixels sea hex válido;
 * devuelve el tamaño binario representado, pero no reconstruye ni muestra la
 * imagen en este demo de comunicación.
 */
static int parse_send_chunk(const protocol_frame_t *frame, uint32_t *message_id,
                            uint32_t *chunk_index, uint32_t *total_chunks,
                            uint32_t *channels, size_t *pixel_bytes) {
    if (frame == NULL || message_id == NULL || chunk_index == NULL ||
        total_chunks == NULL || channels == NULL || pixel_bytes == NULL ||
        frame->length == 0 || frame->length > PROTOCOL_PAYLOAD_BYTES)
        return -1;

    char json[DEMO_JSON_BUFFER + 1u] = {0};
    memcpy(json, frame->payload, frame->length);
    JsonField message_fields[] = {
        {.name = "id", .pair = {0}},
        {.name = "type", .pair = {0}},
        {.name = "command", .pair = {0}},
        {.name = "data", .pair = {0}},
    };
    if (read_object(json, frame->length, message_fields, COUNT(message_fields)) != JSON_PARSE_OK)
        return -1;

    if (read_uint(&message_fields[0], message_id, sizeof(*message_id)) != JSON_PARSE_OK ||
        !json_string_is(&message_fields[1], "request") ||
        !json_string_is(&message_fields[2], "send"))
        return -1;

    JsonField data_fields[] = {
        {.name = "width", .pair = {0}},
        {.name = "height", .pair = {0}},
        {.name = "format", .pair = {0}},
        {.name = "chunk", .pair = {0}},
        {.name = "total_chunks", .pair = {0}},
        {.name = "pixels", .pair = {0}},
    };
    const JSONPair_t *data = &message_fields[3].pair;
    if (data->value == NULL ||
        read_object(data->value, data->valueLength, data_fields, COUNT(data_fields)) != JSON_PARSE_OK)
        return -1;

    uint32_t width = 0, height = 0;
    if (read_uint(&data_fields[0], &width, sizeof(width)) != JSON_PARSE_OK ||
        read_uint(&data_fields[1], &height, sizeof(height)) != JSON_PARSE_OK ||
        read_uint(&data_fields[3], chunk_index, sizeof(*chunk_index)) != JSON_PARSE_OK ||
        read_uint(&data_fields[4], total_chunks, sizeof(*total_chunks)) != JSON_PARSE_OK ||
        width != DEMO_WIDTH || height != DEMO_HEIGHT || *total_chunks == 0 ||
        *chunk_index >= *total_chunks)
        return -1;

    if (json_string_is(&data_fields[2], "GRAY8"))
        *channels = 1u;
    else if (json_string_is(&data_fields[2], "BGR888"))
        *channels = 3u;
    else
        return -1;
    if (*total_chunks > DEMO_WIDTH * DEMO_HEIGHT * *channels)
        return -1;

    const JSONPair_t *pixels = &data_fields[5].pair;
    if (pixels->jsonType != JSONString || pixels->value == NULL ||
        pixels->valueLength == 0 || (pixels->valueLength % 2u) != 0)
        return -1;
    for (size_t i = 0; i < pixels->valueLength; ++i) {
        char c = pixels->value[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
              (c >= 'A' && c <= 'F')))
            return -1;
    }

    *pixel_bytes = pixels->valueLength / 2u;
    return 0;
}

/* Construye y envía la respuesta JSON final como una trama del protocolo. */
static int send_application_response(uint32_t message_id, const char *status) {
    char json[PROTOCOL_PAYLOAD_BYTES + 1u];
    int length = snprintf(json, sizeof(json),
                          "{\"id\":%" PRIu32 ",\"type\":\"response\",\"command\":\"send\","
                          "\"data\":{\"status\":\"%s\"}}",
                          message_id, status);
    if (length <= 0 || (size_t)length > PROTOCOL_PAYLOAD_BYTES)
        return -1;

    protocol_tx_t tx = {0};
    serial_packet_t tx_packet = {0};
    protocol_frame_t frame = {0};
    frame.relevance = 1u;
    frame.length = (uint32_t)length;
    memcpy(frame.payload, json, (size_t)length);

    /* TX initialization clears its buffer, so build the frame afterward. */
    if (protocol_tx_init(&tx, &tx_packet) != PROTOCOL_OK ||
        protocol_set_frame_to_buffer(&frame, tx.buffer, sizeof(tx.buffer)) != PROTOCOL_OK)
        return -1;

    while (!tx.complete) {
        if (protocol_tx_poll(&demo_uart, &tx, &tx_packet) != PROTOCOL_OK)
            return -1;
        /* UART read in the ACK state already blocks for the configured timeout. */
    }
    return tx.valid ? 0 : -1;
}

/* Descarta el estado de ensamblado y apaga los LEDs de etapas persistentes. */
static void reset_transfer(demo_transfer_t *transfer) {
    memset(transfer, 0, sizeof(*transfer));
    board_gpio_write(LED_COMM_START, GPIO_LOW);
    board_gpio_write(LED_TRANSFER_DONE, GPIO_LOW);
}

/*
 * Bucle principal del demo: recibe tramas, valida chunks consecutivos, acumula
 * el tamaño lógico, actualiza los LEDs y responde al completar la imagen.
 */
static void run_demo(void) {
    demo_transfer_t transfer = {0};
    int64_t packet_led_off_at_us = 0;

    for (;;) {
        if (packet_led_off_at_us != 0 && esp_timer_get_time() >= packet_led_off_at_us) {
            board_gpio_write(LED_PACKET, GPIO_LOW);
            packet_led_off_at_us = 0;
        }

        if (protocol_rx_poll(&demo_uart, &demo_rx, &demo_rx_packet) != PROTOCOL_OK) {
            ESP_LOGE(DEMO_TAG, "Fallo en la recepcion del protocolo");
            fail_with_led_pattern();
        }

        if (!demo_rx.complete) {
            /* serial_read already waits up to one RTOS tick when UART has no data. */
            continue;
        }

        if (!demo_rx.valid) {
            protocol_rx_init(&demo_rx, &demo_rx_packet);
            continue;
        }

        protocol_frame_t frame = {0};
        uint32_t id = 0, chunk = 0, total_chunks = 0, channels = 0;
        size_t pixel_bytes = 0;
        int valid = protocol_set_buffer_to_frame(&frame, demo_rx.buffer,
                                                 sizeof(demo_rx.buffer)) == PROTOCOL_OK &&
                    frame.version == PROTOCOL_VERSION &&
                    parse_send_chunk(&frame, &id, &chunk, &total_chunks, &channels,
                                     &pixel_bytes) == 0;

        if (!valid) {
            ESP_LOGW(DEMO_TAG, "Mensaje JSON o trama no compatible; se descarta");
            reset_transfer(&transfer);
            protocol_rx_init(&demo_rx, &demo_rx_packet);
            continue;
        }

        if (chunk == 0u) {
            reset_transfer(&transfer);
            transfer.active = true;
            transfer.id = id;
            transfer.total_chunks = total_chunks;
            transfer.channels = channels;
            board_gpio_write(LED_COMM_START, GPIO_HIGH);
        }

        if (!transfer.active || transfer.id != id || transfer.total_chunks != total_chunks ||
            transfer.channels != channels || transfer.next_chunk != chunk) {
            ESP_LOGW(DEMO_TAG, "Chunk fuera de orden o transferencia inconsistente");
            reset_transfer(&transfer);
            protocol_rx_init(&demo_rx, &demo_rx_packet);
            continue;
        }

        size_t expected_bytes = (size_t)DEMO_WIDTH * DEMO_HEIGHT * transfer.channels;
        if (pixel_bytes == 0 || transfer.received_pixel_bytes > expected_bytes ||
            pixel_bytes > expected_bytes - transfer.received_pixel_bytes) {
            ESP_LOGW(DEMO_TAG, "El chunk excede el tamaño de imagen esperado");
            reset_transfer(&transfer);
            protocol_rx_init(&demo_rx, &demo_rx_packet);
            continue;
        }
        transfer.received_pixel_bytes += pixel_bytes;
        ++transfer.next_chunk;
        board_gpio_write(LED_PACKET, GPIO_HIGH);
        packet_led_off_at_us = esp_timer_get_time() + (int64_t)DEMO_PULSE_MS * 1000;

        if (transfer.next_chunk == transfer.total_chunks) {
            if (transfer.received_pixel_bytes != expected_bytes) {
                ESP_LOGE(DEMO_TAG, "Transferencia incompleta: %zu de %zu bytes, %" PRIu32 " chunks",
                         transfer.received_pixel_bytes, expected_bytes, transfer.total_chunks);
                reset_transfer(&transfer);
                fail_with_led_pattern();
            }
            /* Signal reception/completion before waiting for the host response ACK. */
            board_gpio_write(LED_TRANSFER_DONE, GPIO_HIGH);
            ESP_LOGI(DEMO_TAG, "Transferencia completa: %" PRIu32 " chunks, %zu bytes",
                     transfer.total_chunks, transfer.received_pixel_bytes);
            if (send_application_response(transfer.id, "ok") < 0) {
                ESP_LOGE(DEMO_TAG, "Imagen recibida, pero fallo la respuesta al host");
                fail_with_led_pattern();
            }
            transfer.active = false;
        }

        protocol_rx_init(&demo_rx, &demo_rx_packet);
    }
}

/* Punto de entrada ESP-IDF para el firmware alternativo de demostración. */
void app_main(void) {
    /* Keep the UART1 data channel free of log output; diagnostics use the LEDs. */
    esp_log_level_set("*", ESP_LOG_NONE);
    if (init_demo_hardware() < 0) {
        fail_with_led_pattern();
    }
    run_demo();
}
