/* SPDX-License-Identifier: Apache-2.0 */
#include "network/conversation_http_trace.h"
#include "esp_heap_caps.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);                        \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static char *s_logs[2048];
static size_t s_log_count;
static int s_fail_after = -1;
static size_t s_live_allocations;
static unsigned int s_tests;

void test_log(const char *format, ...) {
    CHECK(s_log_count < sizeof(s_logs) / sizeof(s_logs[0]));
    va_list arguments;
    va_start(arguments, format);
    va_list copy;
    va_copy(copy, arguments);
    int size = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    CHECK(size >= 0);
    char *message = malloc((size_t)size + 1U);
    CHECK(message != NULL);
    CHECK(vsnprintf(message, (size_t)size + 1U, format, arguments) == size);
    va_end(arguments);
    s_logs[s_log_count++] = message;
}

void *heap_caps_malloc(size_t size, unsigned int caps) {
    CHECK(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (s_fail_after == 0) {
        return NULL;
    }
    if (s_fail_after > 0) {
        --s_fail_after;
    }
    void *pointer = malloc(size);
    CHECK(pointer != NULL);
    ++s_live_allocations;
    return pointer;
}

void *heap_caps_calloc(size_t count, size_t size, unsigned int caps) {
    void *pointer = heap_caps_malloc(count * size, caps);
    if (pointer) {
        memset(pointer, 0, count * size);
    }
    return pointer;
}

void heap_caps_free(void *pointer) {
    if (pointer) {
        CHECK(s_live_allocations > 0);
        --s_live_allocations;
        free(pointer);
    }
}

static void reset(void) {
    CHECK(s_live_allocations == 0);
    for (size_t i = 0; i < s_log_count; ++i) {
        free(s_logs[i]);
    }
    s_log_count = 0;
    s_fail_after = -1;
    ++s_tests;
}

static bool logged(const char *text) {
    for (size_t i = 0; i < s_log_count; ++i) {
        if (strstr(s_logs[i], text)) {
            return true;
        }
    }
    return false;
}

static unsigned int first_id(void) {
    unsigned int id = 0;
    CHECK(s_log_count > 0);
    CHECK(sscanf(s_logs[0], "event=http_body id=%u", &id) == 1);
    return id;
}

static void check_body(unsigned int id, const char *direction, const char *expected,
                       bool complete) {
    char prefix[128];
    snprintf(prefix, sizeof(prefix), "event=http_body id=%u direction=%s phase=data offset=", id,
             direction);
    size_t offset = 0;
    size_t expected_size = strlen(expected);
    for (size_t i = 0; i < s_log_count; ++i) {
        const char *line = s_logs[i];
        if (strncmp(line, prefix, strlen(prefix)) != 0) {
            continue;
        }
        size_t recorded_offset;
        CHECK(sscanf(line + strlen(prefix), "%zu", &recorded_offset) == 1);
        CHECK(recorded_offset == offset);
        const char *payload = strstr(line, " data=");
        CHECK(payload != NULL);
        payload += strlen(" data=");
        size_t size = strlen(payload);
        CHECK(size > 0 && size <= 256);
        CHECK(offset + size <= expected_size);
        CHECK(memcmp(expected + offset, payload, size) == 0);
        /* Every chunk begins at a UTF-8 boundary for these valid test bodies. */
        CHECK(((unsigned char)payload[0] & 0xc0U) != 0x80U);
        offset += size;
    }
    CHECK(offset == expected_size);
    char end[160];
    snprintf(end, sizeof(end), "event=http_body id=%u direction=%s phase=end bytes=%zu complete=%s",
             id, direction, expected_size, complete ? "yes" : "no");
    CHECK(logged(end));
}

static char *make_request(const char *path, const char *body, size_t claimed_size) {
    size_t capacity = strlen(path) + strlen(body) + 256;
    char *request = malloc(capacity);
    CHECK(request != NULL);
    int size = snprintf(request, capacity,
                        "POST %s HTTP/1.1\r\nHost: synthetic.invalid\r\n"
                        "Authorization: Bearer synthetic-secret\r\nContent-Length: %zu\r\n\r\n%s",
                        path, claimed_size, body);
    CHECK(size > 0 && (size_t)size < capacity);
    return request;
}

static void send_request(mybot_conversation_http_trace_t *trace, const char *body,
                         size_t fragment) {
    char *request = make_request("/api/devices/synthetic/conversations/start", body, strlen(body));
    size_t size = strlen(request);
    for (size_t offset = 0; offset < size;) {
        size_t count = size - offset < fragment ? size - offset : fragment;
        /* A successful TLS short write feeds only this accepted prefix. */
        mybot_conversation_http_trace_send(trace, request + offset, count);
        offset += count;
    }
    free(request);
}

static void feed_response(mybot_conversation_http_trace_t *trace, const char *response,
                          size_t fragment) {
    size_t size = strlen(response);
    for (size_t offset = 0; offset < size;) {
        size_t count = size - offset < fragment ? size - offset : fragment;
        mybot_conversation_http_trace_recv(trace, response + offset, count);
        offset += count;
    }
}

static char *make_response(int status, const char *body) {
    size_t capacity = strlen(body) + 256;
    char *response = malloc(capacity);
    CHECK(response != NULL);
    int size = snprintf(response, capacity,
                        "HTTP/1.1 %d Synthetic\r\ncontent-length: %zu\r\n"
                        "Set-Cookie: synthetic-private-cookie\r\n\r\n%s",
                        status, strlen(body), body);
    CHECK(size > 0 && (size_t)size < capacity);
    return response;
}

static void test_fragmented_and_large(void) {
    const size_t fragments[] = {1, 2, 7, 256, 4096};
    char body[12001];
    for (size_t i = 0; i < sizeof(body) - 1; i += 3) {
        memcpy(body + i, "中", 3);
    }
    body[sizeof(body) - 1] = '\0';
    for (size_t i = 0; i < sizeof(fragments) / sizeof(fragments[0]); ++i) {
        reset();
        mybot_conversation_http_trace_t *trace = mybot_conversation_http_trace_create();
        CHECK(trace != NULL);
        send_request(trace, "{\"test\":\"合成数据\"}", fragments[i]);
        char *response = make_response(200, body);
        feed_response(trace, response, fragments[i]);
        CHECK(s_log_count == 0); /* No logging before TLS exchange finishes. */
        mybot_conversation_http_trace_finish(trace, false, false);
        check_body(first_id(), "request", "{\"test\":\"合成数据\"}", true);
        check_body(first_id(), "response", body, true);
        CHECK(!logged("synthetic-secret"));
        CHECK(!logged("synthetic-private-cookie"));
        free(response);
    }
}

static void test_error_and_chunked(void) {
    reset();
    mybot_conversation_http_trace_t *trace = mybot_conversation_http_trace_create();
    send_request(trace, "{}", 1);
    feed_response(trace,
                  "HTTP/1.1 500 Synthetic\r\nTransfer-Encoding: Chunked\r\n"
                  "Content-Length: 999\r\n\r\n"
                  "5;part=one\r\n{\"err\r\n6\r\nor\":\"x\r\n2\r\n\"}\r\n"
                  "0\r\nX-Private: synthetic-trailer\r\n\r\n",
                  1);
    mybot_conversation_http_trace_finish(trace, true, false);
    check_body(first_id(), "response", "{\"error\":\"x\"}", true);
    CHECK(logged("status=500"));
    CHECK(!logged("part=one"));
    CHECK(!logged("synthetic-trailer"));

    reset();
    trace = mybot_conversation_http_trace_create();
    send_request(trace, "{}", 1000);
    char *response = make_response(400, "{\"error\":\"synthetic\"}");
    feed_response(trace, response, 3);
    mybot_conversation_http_trace_finish(trace, false, false);
    check_body(first_id(), "response", "{\"error\":\"synthetic\"}", true);
    CHECK(logged("status=400"));
    free(response);
}

static void test_partial_and_unframed(void) {
    reset();
    mybot_conversation_http_trace_t *trace = mybot_conversation_http_trace_create();
    char *request = make_request("/x/conversations/start?synthetic=1", "{\"partial", 100);
    mybot_conversation_http_trace_send(trace, request, strlen(request));
    feed_response(trace, "HTTP/1.1 200 OK\r\nContent-Length: 20\r\n\r\n{\"partial", 1);
    mybot_conversation_http_trace_finish(trace, false, true);
    check_body(first_id(), "request", "{\"partial", false);
    check_body(first_id(), "response", "{\"partial", false);
    CHECK(logged("reason=transport_error"));
    free(request);

    for (unsigned int eof = 0; eof < 2; ++eof) {
        reset();
        trace = mybot_conversation_http_trace_create();
        send_request(trace, "{}", 1);
        feed_response(trace, "HTTP/1.0 200 OK\r\n\r\n{\"closed\":true}", 1);
        mybot_conversation_http_trace_finish(trace, eof != 0, false);
        check_body(first_id(), "response", "{\"closed\":true}", eof != 0);
    }

    reset();
    trace = mybot_conversation_http_trace_create();
    send_request(trace, "{}", 1);
    feed_response(trace, "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\na\r\n12345", 1);
    mybot_conversation_http_trace_finish(trace, true, false);
    check_body(first_id(), "response", "12345", false);

    reset();
    trace = mybot_conversation_http_trace_create();
    send_request(trace, "{}", 1);
    feed_response(trace, "HTTP/1.1 200 OK\r\nAuthorization: synthetic-incomplete", 1);
    mybot_conversation_http_trace_finish(trace, true, false);
    check_body(first_id(), "response", "", false);
    CHECK(!logged("synthetic-incomplete"));
}

static void test_other_endpoints_and_concurrency(void) {
    const char *paths[] = {"/conversations/stop", "/conversations/start-malicious", "/status"};
    for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); ++i) {
        reset();
        mybot_conversation_http_trace_t *trace = mybot_conversation_http_trace_create();
        char *request = make_request(paths[i], "synthetic-other-body", 20);
        mybot_conversation_http_trace_send(trace, request, strlen(request));
        feed_response(trace, "HTTP/1.1 200 OK\r\nContent-Length: 13\r\n\r\nsynthetic-other", 1);
        mybot_conversation_http_trace_finish(trace, true, false);
        CHECK(s_log_count == 0);
        free(request);
    }

    reset();
    mybot_conversation_http_trace_t *one = mybot_conversation_http_trace_create();
    mybot_conversation_http_trace_t *two = mybot_conversation_http_trace_create();
    send_request(one, "{\"one\":1}", 1);
    send_request(two, "{\"two\":2}", 2);
    char *first = make_response(200, "response-one");
    char *second = make_response(200, "response-two");
    feed_response(two, second, 1);
    feed_response(one, first, 1);
    mybot_conversation_http_trace_finish(two, false, false);
    unsigned int second_id = first_id();
    mybot_conversation_http_trace_finish(one, false, false);
    check_body(second_id, "request", "{\"two\":2}", true);
    check_body(second_id, "response", "response-two", true);
    check_body(second_id - 1, "request", "{\"one\":1}", true);
    check_body(second_id - 1, "response", "response-one", true);
    free(first);
    free(second);
}

static void test_memory_failure_and_limits(void) {
    reset();
    s_fail_after = 0;
    mybot_conversation_http_trace_t *trace = mybot_conversation_http_trace_create();
    CHECK(trace == NULL);
    mybot_conversation_http_trace_send(trace, "ignored", 7);
    mybot_conversation_http_trace_recv(trace, "ignored", 7);
    mybot_conversation_http_trace_finish(trace, false, true);
    CHECK(logged("reason=no_memory"));

    reset();
    trace = mybot_conversation_http_trace_create();
    send_request(trace, "{}", 1);
    s_fail_after = 0;
    feed_response(trace, "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\n{}", 1);
    mybot_conversation_http_trace_finish(trace, false, false);
    check_body(first_id(), "request", "{}", true);
    check_body(first_id(), "response", "", false);
    CHECK(logged("reason=no_memory"));

    reset();
    trace = mybot_conversation_http_trace_create();
    send_request(trace, "{}", 1);
    char *body = malloc(33001);
    CHECK(body != NULL);
    memset(body, 'x', 33000);
    body[33000] = '\0';
    char *response = make_response(200, body);
    feed_response(trace, response, 7);
    mybot_conversation_http_trace_finish(trace, true, false);
    CHECK(logged("reason=capacity"));
    CHECK(logged("direction=response phase=end bytes="));
    CHECK(!logged("direction=response phase=end bytes=33000 complete=yes"));
    free(response);
    free(body);
}

static void test_binary_body(void) {
    reset();
    mybot_conversation_http_trace_t *trace = mybot_conversation_http_trace_create();
    send_request(trace, "{}", 1);
    feed_response(trace, "HTTP/1.1 500 Synthetic\r\nContent-Length: 600\r\n\r\n", 1);
    unsigned char body[600];
    memset(body, 'x', sizeof(body));
    body[17] = 0;
    body[257] = 'Y';
    body[599] = 'Z';
    for (size_t i = 0; i < sizeof(body); ++i) {
        mybot_conversation_http_trace_recv(trace, body + i, 1);
    }
    mybot_conversation_http_trace_finish(trace, false, false);
    char prefix[128];
    snprintf(prefix, sizeof(prefix),
             "event=http_body id=%u direction=response phase=data offset=", first_id());
    size_t offset = 0;
    for (size_t i = 0; i < s_log_count; ++i) {
        const char *line = s_logs[i];
        if (strncmp(line, prefix, strlen(prefix)) != 0) {
            continue;
        }
        size_t recorded_offset;
        CHECK(sscanf(line + strlen(prefix), "%zu", &recorded_offset) == 1);
        CHECK(recorded_offset == offset);
        const char *payload = strstr(line, " encoding=hex data=");
        CHECK(payload != NULL);
        payload += strlen(" encoding=hex data=");
        size_t encoded_size = strlen(payload);
        CHECK(encoded_size > 0 && encoded_size <= 512 && encoded_size % 2U == 0);
        for (size_t j = 0; j < encoded_size; j += 2) {
            unsigned int byte;
            CHECK(offset < sizeof(body));
            CHECK(sscanf(payload + j, "%2x", &byte) == 1);
            CHECK(byte == body[offset++]);
        }
    }
    CHECK(offset == sizeof(body));
    CHECK(logged("direction=response phase=end bytes=600 complete=yes"));
}

int main(void) {
    test_fragmented_and_large();
    test_error_and_chunked();
    test_partial_and_unframed();
    test_other_endpoints_and_concurrency();
    test_memory_failure_and_limits();
    test_binary_body();
    reset();
    printf("conversation HTTP trace: %u synthetic scenarios passed\n", s_tests - 1);
    return 0;
}
