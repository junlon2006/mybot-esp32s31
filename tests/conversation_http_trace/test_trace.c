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

static bool logged(const char *text) {
    for (size_t i = 0; i < s_log_count; ++i) {
        if (strstr(s_logs[i], text)) {
            return true;
        }
    }
    return false;
}

static const char *body_record(unsigned int id, const char *direction) {
    char prefix[128];
    snprintf(prefix, sizeof(prefix), "event=http_body id=%u direction=%s phase=data ", id,
             direction);
    const char *record = NULL;
    for (size_t i = 0; i < s_log_count; ++i) {
        if (strncmp(s_logs[i], prefix, strlen(prefix)) == 0) {
            CHECK(record == NULL);
            record = s_logs[i] + strlen(prefix);
        }
    }
    CHECK(record != NULL);
    return record;
}

static void reset(void) {
    CHECK(s_live_allocations == 0);
    for (size_t i = 0; i < s_log_count; ++i) {
        if (strstr(s_logs[i], " phase=begin ")) {
            unsigned int id;
            char direction[16];
            CHECK(sscanf(s_logs[i], "event=http_body id=%u direction=%15s", &id, direction) == 2);
            /* Every captured direction has exactly one body record, including empty
             * bodies and explicit output-allocation failures. */
            (void)body_record(id, direction);
        }
    }
    for (size_t i = 0; i < s_log_count; ++i) {
        free(s_logs[i]);
    }
    s_log_count = 0;
    s_fail_after = -1;
    ++s_tests;
}

static unsigned int first_id(void) {
    unsigned int id = 0;
    CHECK(s_log_count > 0);
    CHECK(sscanf(s_logs[0], "event=http_body id=%u", &id) == 1);
    return id;
}

static void check_body(unsigned int id, const char *direction, const char *expected,
                       bool complete) {
    const char *record = body_record(id, direction);
    CHECK(strncmp(record, "data=", strlen("data=")) == 0);
    const char *payload = record + strlen("data=");
    size_t expected_size = strlen(expected);
    CHECK(strlen(payload) == expected_size);
    CHECK(memcmp(payload, expected, expected_size) == 0);
    char end[160];
    snprintf(end, sizeof(end), "event=http_body id=%u direction=%s phase=end bytes=%zu complete=%s",
             id, direction, expected_size, complete ? "yes" : "no");
    CHECK(logged(end));
}

static char *make_method_request(const char *method, const char *path, const char *body,
                                 size_t claimed_size) {
    size_t capacity = strlen(method) + strlen(path) + strlen(body) + 256;
    char *request = malloc(capacity);
    CHECK(request != NULL);
    int size = snprintf(request, capacity,
                        "%s %s HTTP/1.1\r\nHost: synthetic.invalid\r\n"
                        "Authorization: Bearer synthetic-secret\r\nContent-Length: %zu\r\n\r\n%s",
                        method, path, claimed_size, body);
    CHECK(size > 0 && (size_t)size < capacity);
    return request;
}

static char *make_request(const char *path, const char *body, size_t claimed_size) {
    return make_method_request("POST", path, body, claimed_size);
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
        CHECK(logged("method=POST path=/api/devices/<device>/conversations/start"));
        CHECK(!logged("/devices/synthetic/"));
        free(response);
    }
}

static void test_capture_boundaries(void) {
    reset();
    const size_t response_capacity = 32U * 1024U;
    char *body = malloc(response_capacity + 1U);
    CHECK(body != NULL);
    memset(body, 'x', response_capacity);
    body[response_capacity] = '\0';
    char *response = make_response(200, body);
    size_t headers_size = strlen(response) - response_capacity;
    free(response);
    body[response_capacity - headers_size] = '\0';
    response = make_response(200, body);
    CHECK(strlen(response) == response_capacity);

    char request_body[2049];
    memset(request_body, 'r', sizeof(request_body) - 1U);
    request_body[sizeof(request_body) - 1U] = '\0';
    char *request = make_request("/diagnostics", request_body, strlen(request_body));
    headers_size = strlen(request) - strlen(request_body);
    free(request);
    request_body[2048U - headers_size] = '\0';
    request = make_request("/diagnostics", request_body, strlen(request_body));
    CHECK(strlen(request) == 2048U);

    mybot_conversation_http_trace_t *trace = mybot_conversation_http_trace_create();
    CHECK(trace != NULL);
    mybot_conversation_http_trace_send(trace, request, strlen(request));
    feed_response(trace, response, 17);
    /* Text logs use the captured buffers directly, requiring no new allocation. */
    s_fail_after = 0;
    mybot_conversation_http_trace_finish(trace, false, false);
    check_body(first_id(), "request", request_body, true);
    check_body(first_id(), "response", body, true);
    CHECK(!logged("reason=capacity"));
    CHECK(!logged("result=unavailable"));
    free(request);
    free(response);
    free(body);
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
    const char *paths[] = {"/conversations/stop", "/conversations/start-malicious", "/status",
                           "/api/devices/pair-codes"};
    for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); ++i) {
        reset();
        mybot_conversation_http_trace_t *trace = mybot_conversation_http_trace_create();
        char *request = make_request(paths[i], "synthetic-other-body", 20);
        mybot_conversation_http_trace_send(trace, request, strlen(request));
        char *response = make_response(200, "synthetic-other");
        feed_response(trace, response, 1);
        mybot_conversation_http_trace_finish(trace, true, false);
        check_body(first_id(), "request", "synthetic-other-body", true);
        check_body(first_id(), "response", "synthetic-other", true);
        char path[300];
        snprintf(path, sizeof(path), "method=POST path=%s ", paths[i]);
        CHECK(logged(path));
        free(request);
        free(response);
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

static void test_empty_and_other_methods(void) {
    reset();
    mybot_conversation_http_trace_t *trace = mybot_conversation_http_trace_create();
    const char *request =
        "GET /api/devices/synthetic-device/binding-status?synthetic-private-query "
        "HTTP/1.1\r\nAuthorization: synthetic-private-auth\r\n\r\n";
    for (size_t i = 0; i < strlen(request); ++i) {
        mybot_conversation_http_trace_send(trace, request + i, 1);
    }
    char *response = make_response(200, "{\"bound\":true}");
    feed_response(trace, response, 1);
    mybot_conversation_http_trace_finish(trace, false, false);
    check_body(first_id(), "request", "", true);
    check_body(first_id(), "response", "{\"bound\":true}", true);
    CHECK(logged("method=GET path=/api/devices/<device>/binding-status "));
    CHECK(!logged("synthetic-device"));
    CHECK(!logged("synthetic-private-query"));
    CHECK(!logged("synthetic-private-auth"));
    free(response);

    reset();
    trace = mybot_conversation_http_trace_create();
    char *head = make_method_request("HEAD", "/status", "", 0);
    mybot_conversation_http_trace_send(trace, head, strlen(head));
    feed_response(trace, "HTTP/1.1 200 OK\r\nContent-Length: 600\r\n\r\n", 1);
    mybot_conversation_http_trace_finish(trace, false, false);
    check_body(first_id(), "request", "", true);
    check_body(first_id(), "response", "", true);
    CHECK(logged("method=HEAD path=/status "));
    free(head);

    const char *empty_responses[] = {"HTTP/1.1 204 No Content\r\n\r\n",
                                     "HTTP/1.1 304 Not Modified\r\n\r\n",
                                     "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n"};
    for (size_t i = 0; i < sizeof(empty_responses) / sizeof(empty_responses[0]); ++i) {
        reset();
        trace = mybot_conversation_http_trace_create();
        send_request(trace, "", 1);
        feed_response(trace, empty_responses[i], 1);
        mybot_conversation_http_trace_finish(trace, false, false);
        check_body(first_id(), "request", "", true);
        check_body(first_id(), "response", "", true);
    }
}

static void test_send_failures_and_chunked_request(void) {
    reset();
    mybot_conversation_http_trace_t *trace = mybot_conversation_http_trace_create();
    char *request =
        make_request("/api/devices/synthetic-device/conversations/stop", "{\"partial", 20);
    /* The attempted write may have contained more bytes; feed only accepted bytes. */
    mybot_conversation_http_trace_send(trace, request, strlen(request));
    mybot_conversation_http_trace_finish(trace, false, true);
    check_body(first_id(), "request", "{\"partial", false);
    check_body(first_id(), "response", "", false);
    CHECK(logged("reason=transport_error"));
    CHECK(logged("transport_error=yes"));
    char count[100];
    snprintf(count, sizeof(count), "wire_bytes=%zu captured_wire_bytes=%zu", strlen(request),
             strlen(request));
    CHECK(logged(count));
    free(request);

    reset();
    trace = mybot_conversation_http_trace_create();
    send_request(trace, "{}", 1);
    mybot_conversation_http_trace_finish(trace, false, false);
    check_body(first_id(), "request", "{}", true);
    check_body(first_id(), "response", "", false);
    CHECK(logged("reason=no_response"));

    reset();
    trace = mybot_conversation_http_trace_create();
    request = "PUT /diagnostics HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n"
              "3\r\nabc\r\n2\r\nde\r\n0\r\nX-Private: synthetic-private-trailer\r\n\r\n";
    mybot_conversation_http_trace_send(trace, request, strlen(request));
    feed_response(trace, "HTTP/1.1 204 No Content\r\n\r\n", 1);
    mybot_conversation_http_trace_finish(trace, false, false);
    check_body(first_id(), "request", "abcde", true);
    check_body(first_id(), "response", "", true);
    CHECK(logged("method=PUT path=/diagnostics "));
    CHECK(!logged("synthetic-private-trailer"));

    reset();
    trace = mybot_conversation_http_trace_create();
    mybot_conversation_http_trace_send(trace, "POST /synthetic", strlen("POST /synthetic"));
    mybot_conversation_http_trace_finish(trace, false, true);
    check_body(first_id(), "request", "", false);
    check_body(first_id(), "response", "", false);
    CHECK(logged("reason=headers"));
    CHECK(logged("method=unknown path=unknown"));
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
    size_t headers_size = (size_t)(strstr(response, "\r\n\r\n") + 4 - response);
    body[32U * 1024U - headers_size] = '\0';
    check_body(first_id(), "response", body, false);
    CHECK(logged("reason=capacity"));
    CHECK(logged("direction=response phase=end bytes="));
    CHECK(!logged("direction=response phase=end bytes=33000 complete=yes"));
    free(response);
    free(body);

    reset();
    trace = mybot_conversation_http_trace_create();
    body = malloc(4001);
    CHECK(body != NULL);
    memset(body, 'x', 4000);
    body[4000] = '\0';
    char *request = make_request("/conversations/stop", body, 4000);
    mybot_conversation_http_trace_send(trace, request, strlen(request));
    feed_response(trace, "HTTP/1.1 204 No Content\r\n\r\n", 1);
    mybot_conversation_http_trace_finish(trace, false, false);
    headers_size = (size_t)(strstr(request, "\r\n\r\n") + 4 - request);
    body[2048U - headers_size] = '\0';
    check_body(first_id(), "request", body, false);
    CHECK(logged("reason=capacity"));
    CHECK(!logged("direction=request phase=end bytes=4000 complete=yes"));
    char count[100];
    snprintf(count, sizeof(count), "wire_bytes=%zu captured_wire_bytes=2048", strlen(request));
    CHECK(logged(count));
    free(request);
    free(body);
}

static void test_binary_body(void) {
    unsigned char body[600];
    memset(body, 'x', sizeof(body));
    body[17] = 0;
    body[257] = 'Y';
    body[599] = 'Z';
    for (unsigned int fail_encoding = 0; fail_encoding < 2; ++fail_encoding) {
        reset();
        mybot_conversation_http_trace_t *trace = mybot_conversation_http_trace_create();
        send_request(trace, "{}", 1);
        feed_response(trace, "HTTP/1.1 500 Synthetic\r\nContent-Length: 600\r\n\r\n", 1);
        for (size_t i = 0; i < sizeof(body); ++i) {
            mybot_conversation_http_trace_recv(trace, body + i, 1);
        }
        if (fail_encoding) {
            s_fail_after = 0;
        }
        mybot_conversation_http_trace_finish(trace, false, false);
        check_body(first_id(), "request", "{}", true);
        const char *record = body_record(first_id(), "response");
        if (fail_encoding) {
            CHECK(strcmp(record, "encoding=hex result=unavailable reason=no_memory") == 0);
        } else {
            const char *prefix = "encoding=hex data=";
            CHECK(strncmp(record, prefix, strlen(prefix)) == 0);
            const char *payload = record + strlen(prefix);
            CHECK(strlen(payload) == sizeof(body) * 2U);
            for (size_t i = 0; i < sizeof(body); ++i) {
                unsigned int byte;
                CHECK(sscanf(payload + i * 2U, "%2x", &byte) == 1);
                CHECK(byte == body[i]);
            }
        }
        CHECK(logged("direction=response phase=end bytes=600 complete=yes"));
        CHECK(logged("status=500 reason=received"));
        CHECK(s_live_allocations == 0);
    }
}

int main(void) {
    test_fragmented_and_large();
    test_capture_boundaries();
    test_error_and_chunked();
    test_partial_and_unframed();
    test_other_endpoints_and_concurrency();
    test_empty_and_other_methods();
    test_send_failures_and_chunked_request();
    test_memory_failure_and_limits();
    test_binary_body();
    reset();
    printf("conversation HTTP trace: %u synthetic scenarios passed\n", s_tests - 1);
    return 0;
}
