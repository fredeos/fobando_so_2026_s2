#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "comms/comms.h"

static void init_handler(comms_handler_t *handler, uint8_t priority)
{
    memset(handler, 0, sizeof(*handler));
    assert(comms_init_handler(0, handler, "test-device", priority, 0u)
           == COMMS_OK);
}

static void destroy_handler(comms_handler_t *handler)
{
    pthread_cond_destroy(&handler->cond);
    pthread_mutex_destroy(&handler->mutex);
}

static void test_fifo_preserves_messages(void)
{
    comms_handler_t handler;
    init_handler(&handler, 0u);

    char buffer[COMMS_MESSAGE_CAPACITY];
    size_t size = 0u;
    assert(comms_receive_next(&handler, buffer, sizeof(buffer), &size)
           == COMMS_BUSY);

    const char first[] = "{\"type\":\"EVENT\",\"request_id\":1}";
    const char second[] = "{\"type\":\"EVENT\",\"request_id\":2}";
    assert(comms_receive_enqueue(&handler, first, sizeof(first), 0u) == COMMS_OK);
    assert(comms_receive_enqueue(&handler, second, sizeof(second), 0u) == COMMS_OK);

    assert(comms_receive_next(&handler, buffer, sizeof(buffer), &size) == COMMS_OK);
    assert(size == sizeof(first));
    assert(memcmp(buffer, first, sizeof(first)) == 0);
    assert(comms_receive_next(&handler, buffer, sizeof(buffer), &size) == COMMS_OK);
    assert(size == sizeof(second));
    assert(memcmp(buffer, second, sizeof(second)) == 0);
    assert(comms_receive_next(&handler, buffer, sizeof(buffer), &size)
           == COMMS_BUSY);
    destroy_handler(&handler);
}

static void test_heartbeats_do_not_fill_fifo(void)
{
    comms_handler_t handler;
    init_handler(&handler, 0u);

    const char pong[] = "PONG from test";
    for (size_t i = 0u; i < COMMS_RECEIVE_QUEUE_CAPACITY * 2u; ++i)
        assert(comms_receive_enqueue(&handler, pong, sizeof(pong), 0u) == COMMS_OK);
    assert(comms_heartbeat_count(&handler) ==
           COMMS_RECEIVE_QUEUE_CAPACITY * 2u);
    assert(handler.receive_count == 0u);
    assert(comms_reports_drained_since(&handler, 0u));

    uint64_t baseline = comms_heartbeat_count(&handler);
    const char event[] = "{\"type\":\"EVENT\"}";
    assert(comms_receive_enqueue(&handler, event, sizeof(event), 0u) == COMMS_OK);
    assert(comms_receive_enqueue(&handler, pong, sizeof(pong), 0u) == COMMS_OK);
    assert(!comms_reports_drained_since(&handler, baseline));
    char buffer[COMMS_MESSAGE_CAPACITY];
    size_t size = 0u;
    assert(comms_receive_next(&handler, buffer, sizeof(buffer), &size) == COMMS_OK);
    assert(comms_reports_drained_since(&handler, baseline));
    destroy_handler(&handler);
}

static void test_bad_frame_count_is_drained(void)
{
    comms_handler_t handler;
    init_handler(&handler, 0u);

    comms_note_bad_frame(&handler);
    comms_note_bad_frame(&handler);
    assert(comms_take_bad_frame_count(&handler) == 2u);
    assert(comms_take_bad_frame_count(&handler) == 0u);
    comms_note_bad_frame(&handler);
    assert(comms_take_bad_frame_count(&handler) == 1u);

    destroy_handler(&handler);
}

static void test_overflow_is_reported_without_reordering(void)
{
    comms_handler_t handler;
    init_handler(&handler, 0u);

    const char message[] = "payload";
    char buffer[COMMS_MESSAGE_CAPACITY];
    size_t size = 0u;
    for (size_t i = 0u; i < COMMS_RECEIVE_QUEUE_CAPACITY; ++i)
        assert(comms_receive_enqueue(&handler, message, sizeof(message), 0u)
               == COMMS_OK);
    assert(comms_receive_enqueue(&handler, message, sizeof(message), 0u)
           == COMMS_RX_ERROR);
    assert(comms_receive_next(&handler, buffer, sizeof(buffer), &size)
           == COMMS_RX_ERROR);

    for (size_t i = 0u; i < COMMS_RECEIVE_QUEUE_CAPACITY; ++i) {
        assert(comms_receive_next(&handler, buffer, sizeof(buffer), &size)
               == COMMS_OK);
        assert(size == sizeof(message));
        assert(memcmp(buffer, message, sizeof(message)) == 0);
    }
    assert(comms_receive_next(&handler, buffer, sizeof(buffer), &size)
           == COMMS_BUSY);
    destroy_handler(&handler);
}

static void test_priority_filter_and_forced_receive(void)
{
    comms_handler_t handler;
    init_handler(&handler, 2u);

    const char message[] = "low priority";
    char buffer[COMMS_MESSAGE_CAPACITY];
    size_t size = 0u;
    assert(comms_receive_enqueue(&handler, message, sizeof(message), 1u)
           == COMMS_BUSY);
    assert(comms_request_receive(&handler) == COMMS_OK);
    assert(comms_receive_enqueue(&handler, message, sizeof(message), 1u)
           == COMMS_OK);
    assert(comms_receive_next(&handler, buffer, sizeof(buffer), &size) == COMMS_OK);
    assert(size == sizeof(message));
    assert(memcmp(buffer, message, sizeof(message)) == 0);
    destroy_handler(&handler);
}

int main(void)
{
    test_fifo_preserves_messages();
    test_heartbeats_do_not_fill_fifo();
    test_bad_frame_count_is_drained();
    test_overflow_is_reported_without_reordering();
    test_priority_filter_and_forced_receive();
    puts("OK: serial receive FIFO tests");
    return 0;
}
