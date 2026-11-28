#define _DEFAULT_SOURCE

#include "driver/screen_driver.h"

#include "common/protocol.h"
#include "serial/posix.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define DRIVER_POLL_DELAY_NS 1000000L
#define DRIVER_RESPONSE_TIMEOUT_MS 5000u
#define DRIVER_HEX_PREFIX_CAPACITY 192u

/* Pausa breve para evitar consulta activa al esperar datos de respuesta/ACK. */
static void poll_delay(void) {
    const struct timespec delay = {0, DRIVER_POLL_DELAY_NS};
    nanosleep(&delay, NULL);
}

/*
 * Encapsula un payload de aplicación en una trama del protocolo existente y
 * avanza su máquina TX hasta recibir ACK/NACK o agotar los intentos.
 */
static screen_driver_status_t send_protocol_payload(screen_driver_t *driver,
                                                    const uint8_t *payload,
                                                    size_t payload_size) {
    protocol_frame_t frame = {0};
    protocol_tx_t tx = {0};
    serial_packet_t packet = {0};

    if (payload == NULL || payload_size == 0 || payload_size > PROTOCOL_PAYLOAD_BYTES)
        return SCREEN_DRIVER_INVALID_ARGUMENT;

    frame.relevance = 1u;
    frame.length = (uint32_t)payload_size;
    memcpy(frame.payload, payload, payload_size);
    /* protocol_tx_init clears tx.buffer; serialize the frame only after init. */
    if (protocol_tx_init(&tx, &packet) != PROTOCOL_OK ||
        protocol_set_frame_to_buffer(&frame, tx.buffer, sizeof(tx.buffer)) != PROTOCOL_OK)
        return SCREEN_DRIVER_PROTOCOL_ERROR;

    for (unsigned attempt = 0; attempt < PROTOCOL_MAX_ATTEMPTS && !tx.complete; ++attempt) {
        if (protocol_tx_poll(&driver->serial_fd, &tx, &packet) != PROTOCOL_OK)
            return SCREEN_DRIVER_PROTOCOL_ERROR;
        /* State transitions and UART writes are immediate; only ACK polling needs pacing. */
        if (!tx.complete && tx.state == PROTOCOL_TX_ACK)
            poll_delay();
    }
    return tx.complete && tx.valid ? SCREEN_DRIVER_OK : SCREEN_DRIVER_PROTOCOL_ERROR;
}

/*
 * Espera la respuesta de aplicación al terminar todos los chunks. A diferencia
 * del ACK binario (por trama), esta respuesta confirma que el ESP32 aceptó la
 * transferencia completa y debe incluir el mismo ID de mensaje.
 */
static screen_driver_status_t receive_application_response(screen_driver_t *driver,
                                                           uint32_t message_id) {
    protocol_rx_t rx = {0};
    serial_packet_t packet = {0};
    const unsigned max_polls = DRIVER_RESPONSE_TIMEOUT_MS * 1000u;

    if (protocol_rx_init(&rx, &packet) != PROTOCOL_OK)
        return SCREEN_DRIVER_PROTOCOL_ERROR;

    for (unsigned poll = 0; poll < max_polls && !rx.complete; ++poll) {
        if (protocol_rx_poll(&driver->serial_fd, &rx, &packet) != PROTOCOL_OK)
            return SCREEN_DRIVER_PROTOCOL_ERROR;
        if (!rx.complete)
            poll_delay();
    }
    if (!rx.complete || !rx.valid)
        return SCREEN_DRIVER_PROTOCOL_ERROR;

    protocol_frame_t response = {0};
    if (protocol_set_buffer_to_frame(&response, rx.buffer, sizeof(rx.buffer)) != PROTOCOL_OK ||
        response.version != PROTOCOL_VERSION || response.length == 0 ||
        response.length > PROTOCOL_PAYLOAD_BYTES)
        return SCREEN_DRIVER_PROTOCOL_ERROR;

    /* Application response contract: same id, response/send, data.status=ok. */
    char expected[128];
    int length = snprintf(expected, sizeof(expected),
                          "{\"id\":%u,\"type\":\"response\",\"command\":\"send\","
                          "\"data\":{\"status\":\"ok\"}}",
                          message_id);
    if (length < 0 || (size_t)length != response.length ||
        memcmp(response.payload, expected, response.length) != 0)
        return SCREEN_DRIVER_DEVICE_REJECTED;

    return SCREEN_DRIVER_OK;
}

/*
 * Calcula cuántos bytes originales de píxeles caben en un payload JSON,
 * descontando prefijo/sufijo y considerando que cada byte se expande a dos
 * caracteres hexadecimales. Retorna cero si los metadatos no caben.
 */
static size_t chunk_capacity(uint32_t message_id, uint32_t total_chunks,
                             uint32_t chunk_index, const char *format) {
    char prefix[DRIVER_HEX_PREFIX_CAPACITY];
    int prefix_size = snprintf(prefix, sizeof(prefix),
                               "{\"id\":%u,\"type\":\"request\",\"command\":\"send\","
                               "\"data\":{\"width\":%u,\"height\":%u,\"format\":\"%s\","
                               "\"chunk\":%u,\"total_chunks\":%u,\"pixels\":\"",
                               message_id, SCREEN_DRIVER_WIDTH, SCREEN_DRIVER_HEIGHT,
                               format, chunk_index, total_chunks);
    const size_t suffix_size = sizeof("\"}}") - 1u;
    if (prefix_size < 0 || (size_t)prefix_size + suffix_size >= PROTOCOL_PAYLOAD_BYTES)
        return 0;
    return (PROTOCOL_PAYLOAD_BYTES - (size_t)prefix_size - suffix_size) / 2u;
}

/* Construye el JSON de un chunk y lo entrega al encapsulador del protocolo. */
static screen_driver_status_t send_image_chunk(screen_driver_t *driver, uint32_t message_id,
                                               uint32_t chunk_index, uint32_t total_chunks,
                                               const uint8_t *pixels, size_t byte_count,
                                               const char *format) {
    char payload[PROTOCOL_PAYLOAD_BYTES + 1u];
    int prefix_size = snprintf(payload, sizeof(payload),
                               "{\"id\":%u,\"type\":\"request\",\"command\":\"send\","
                               "\"data\":{\"width\":%u,\"height\":%u,\"format\":\"%s\","
                               "\"chunk\":%u,\"total_chunks\":%u,\"pixels\":\"",
                               message_id, SCREEN_DRIVER_WIDTH, SCREEN_DRIVER_HEIGHT,
                               format, chunk_index, total_chunks);
    if (prefix_size < 0 || (size_t)prefix_size >= sizeof(payload))
        return SCREEN_DRIVER_ERROR;

    size_t offset = (size_t)prefix_size;
    for (size_t i = 0; i < byte_count; ++i) {
        int n = snprintf(payload + offset, sizeof(payload) - offset, "%02x", pixels[i]);
        if (n != 2)
            return SCREEN_DRIVER_ERROR;
        offset += 2;
    }
    if (sizeof(payload) - offset < sizeof("\"}}"))
        return SCREEN_DRIVER_ERROR;
    memcpy(payload + offset, "\"}}", sizeof("\"}}"));
    offset += sizeof("\"}}") - 1u;
    return send_protocol_payload(driver, (const uint8_t *)payload, offset);
}

/* Inicializa el contexto y configura el descriptor serial del anfitrión. */
screen_driver_status_t screen_driver_open(screen_driver_t *driver,
                                          const char *serial_device,
                                          unsigned int baudrate) {
    if (driver == NULL || serial_device == NULL || serial_device[0] == '\0' || baudrate == 0)
        return SCREEN_DRIVER_INVALID_ARGUMENT;

    driver->serial_fd = -1;
    driver->next_message_id = 1u;
    driver->initialized = 0u;
    posix_serial_config_t config = {serial_device, baudrate};
    if (serial_configure(&driver->serial_fd, &config) != SERIAL_OK)
        return SCREEN_DRIVER_ERROR;
    driver->initialized = 1u;
    return SCREEN_DRIVER_OK;
}

/*
 * Implementación principal del envío. Valida imagen/contexto, asigna un ID,
 * converge el conteo de chunks con el tamaño real de sus metadatos, envía cada
 * fragmento de forma secuencial y notifica el callback tras su ACK. Finalmente
 * espera la respuesta JSON de aceptación del ESP32.
 */
screen_driver_status_t screen_driver_send_image_with_progress(
    screen_driver_t *driver, const uint8_t *pixels, size_t size,
    uint32_t width, uint32_t height, uint8_t channels,
    screen_driver_progress_fn progress, void *context) {
    if (driver == NULL || pixels == NULL || !driver->initialized)
        return SCREEN_DRIVER_NOT_INITIALIZED;
    if (width != SCREEN_DRIVER_WIDTH || height != SCREEN_DRIVER_HEIGHT ||
        (channels != 1u && channels != 3u))
        return SCREEN_DRIVER_INVALID_ARGUMENT;

    const size_t expected_size = (size_t)width * height * channels;
    if (size != expected_size)
        return SCREEN_DRIVER_INVALID_ARGUMENT;

    const char *format = channels == 1u ? "GRAY8" : "BGR888";
    uint32_t message_id = driver->next_message_id++;
    if (driver->next_message_id == 0u)
        driver->next_message_id = 1u;

    /* Chunk count and per-chunk capacity converge as their decimal widths settle. */
    uint32_t total_chunks = 1u;
    size_t bytes_per_chunk = 0;
    for (unsigned iteration = 0; iteration < 8u; ++iteration) {
        bytes_per_chunk = chunk_capacity(message_id, total_chunks, total_chunks - 1u, format);
        if (bytes_per_chunk == 0)
            return SCREEN_DRIVER_ERROR;
        uint32_t calculated = (uint32_t)((size + bytes_per_chunk - 1u) / bytes_per_chunk);
        if (calculated == total_chunks)
            break;
        total_chunks = calculated;
    }

    /* Recompute capacity using the final chunk-count/index digit widths. */
    bytes_per_chunk = chunk_capacity(message_id, total_chunks, total_chunks - 1u, format);
    if (bytes_per_chunk == 0)
        return SCREEN_DRIVER_ERROR;
    uint32_t verified_chunks = (uint32_t)((size + bytes_per_chunk - 1u) / bytes_per_chunk);
    if (verified_chunks != total_chunks) {
        total_chunks = verified_chunks;
        bytes_per_chunk = chunk_capacity(message_id, total_chunks, total_chunks - 1u, format);
        if (bytes_per_chunk == 0 || (size + bytes_per_chunk - 1u) / bytes_per_chunk != total_chunks)
            return SCREEN_DRIVER_ERROR;
    }

    for (uint32_t chunk = 0; chunk < total_chunks; ++chunk) {
        size_t offset = (size_t)chunk * bytes_per_chunk;
        size_t remaining = size - offset;
        size_t count = remaining < bytes_per_chunk ? remaining : bytes_per_chunk;
        /* The final index has the same or fewer digits than total_chunks - 1. */
        size_t actual_capacity = chunk_capacity(message_id, total_chunks, chunk, format);
        if (count > actual_capacity)
            return SCREEN_DRIVER_ERROR;
        screen_driver_status_t sent = send_image_chunk(driver, message_id, chunk,
                                                       total_chunks, pixels + offset,
                                                       count, format);
        if (sent != SCREEN_DRIVER_OK)
            return sent;
        if (progress != NULL)
            progress(chunk + 1u, total_chunks, context);
    }

    return receive_application_response(driver, message_id);
}

/* Variante sin callback para consumidores que no requieren progreso. */
screen_driver_status_t screen_driver_send_image(screen_driver_t *driver,
                                               const uint8_t *pixels,
                                               size_t size,
                                               uint32_t width,
                                               uint32_t height,
                                               uint8_t channels) {
    return screen_driver_send_image_with_progress(driver, pixels, size, width,
                                                 height, channels, NULL, NULL);
}

/* Cierra el descriptor y limpia los campos de estado del contexto. */
screen_driver_status_t screen_driver_close(screen_driver_t *driver) {
    if (driver == NULL)
        return SCREEN_DRIVER_INVALID_ARGUMENT;
    if (!driver->initialized)
        return SCREEN_DRIVER_OK;

    int result = close(driver->serial_fd);
    driver->serial_fd = -1;
    driver->initialized = 0u;
    return result == 0 ? SCREEN_DRIVER_OK : SCREEN_DRIVER_ERROR;
}
