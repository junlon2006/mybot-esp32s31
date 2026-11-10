/* SPDX-License-Identifier: Apache-2.0 */
#include "mybot_http_client.h"
#include "mybot_https_transport.h"

#include <hal/aosl_hal_memory.h>
#include <hal/aosl_hal_socket.h>
#include <hal/aosl_hal_time.h>

#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define URL "https://translation.example.invalid/api/devices/test/conversations/start"
#define RESPONSE "HTTP/1.1 201 Created\r\nContent-Length: 2\r\n\r\n{}"
#define MAX_STEPS 8

typedef struct {
    const char *data;
    int delay_ms;
} recv_step_t;

static uint64_t tick_ms;
static unsigned allocations, connects, sends, receives, closes, tests;
static unsigned successful_connects, allocation_calls, fail_allocation_at;
static int connect_delay_ms, send_delay_ms, connect_timeout;
static size_t send_limit;
static int receive_timeouts[MAX_STEPS], send_timeouts[MAX_STEPS];
static recv_step_t steps[MAX_STEPS];
static size_t step_count;
static bool connected;

uint64_t aosl_hal_get_tick_ms(void) {
    return tick_ms;
}

void aosl_hal_msleep(uint64_t ms) {
    (void)ms;
    assert(!"HTTPS tests must never sleep");
}

void *aosl_hal_malloc(size_t size) {
    if (++allocation_calls == fail_allocation_at)
        return NULL;
    void *allocation = malloc(size);
    assert(allocation);
    ++allocations;
    return allocation;
}

void *aosl_hal_realloc(void *memory, size_t size) {
    assert(memory && size > 0);
    void *allocation = realloc(memory, size);
    assert(allocation);
    return allocation;
}

void aosl_hal_free(void *memory) {
    if (memory) {
        assert(allocations > 0);
        --allocations;
        free(memory);
    }
}

/* Plain HTTP must never be reached. The real HTTP stream has these fallback
 * branches even when the build configuration rejects every plaintext URL. */
int aosl_hal_sk_send(aosl_fd_t fd, const void *data, size_t length, int flags) {
    (void)fd;
    (void)data;
    (void)length;
    (void)flags;
    assert(!"plaintext send");
    return -1;
}

int aosl_hal_sk_recv(aosl_fd_t fd, void *data, size_t capacity, int flags) {
    (void)fd;
    (void)data;
    (void)capacity;
    (void)flags;
    assert(!"plaintext receive");
    return -1;
}

int aosl_hal_sk_close(aosl_fd_t fd) {
    (void)fd;
    assert(!"plaintext close");
    return -1;
}

static bool advance(int delay_ms, int timeout_ms) {
    assert(delay_ms >= 0 && timeout_ms > 0);
    if (delay_ms > timeout_ms) {
        tick_ms += (uint64_t)timeout_ms;
        return false;
    }
    tick_ms += (uint64_t)delay_ms;
    return true;
}

int mybot_https_transport_connect(void **connection, const char *host, uint16_t port,
                                  int timeout_ms) {
    assert(connection && strcmp(host, "translation.example.invalid") == 0 && port == 443);
    assert(!connected);
    ++connects;
    connect_timeout = timeout_ms;
    *connection = NULL;
    if (!advance(connect_delay_ms, timeout_ms))
        return -1;
    connected = true;
    ++successful_connects;
    *connection = &connected;
    return 0;
}

int mybot_https_transport_send(void *connection, const void *data, size_t length, int timeout_ms) {
    assert(connection == &connected && connected && data && length > 0);
    assert(sends < MAX_STEPS);
    send_timeouts[sends++] = timeout_ms;
    if (!advance(send_delay_ms, timeout_ms))
        return -1;
    return (int)(send_limit && length > send_limit ? send_limit : length);
}

int mybot_https_transport_recv(void *connection, void *data, size_t capacity, int timeout_ms) {
    assert(connection == &connected && connected && data && capacity > 0);
    assert(receives < step_count);
    size_t index = receives++;
    receive_timeouts[index] = timeout_ms;
    if (!advance(steps[index].delay_ms, timeout_ms))
        return -1;
    if (!steps[index].data)
        return 0;
    size_t length = strlen(steps[index].data);
    assert(length <= capacity);
    memcpy(data, steps[index].data, length);
    return (int)length;
}

void mybot_https_transport_close(void *connection) {
    assert(connection == &connected && connected);
    connected = false;
    ++closes;
}

static void reset(void) {
    assert(!connected && allocations == 0);
    tick_ms = 1000;
    connects = sends = receives = closes = 0;
    successful_connects = allocation_calls = fail_allocation_at = 0;
    connect_delay_ms = 300;
    send_delay_ms = 100;
    connect_timeout = 0;
    send_limit = 0;
    step_count = 0;
    memset(steps, 0, sizeof(steps));
    memset(receive_timeouts, 0, sizeof(receive_timeouts));
    memset(send_timeouts, 0, sizeof(send_timeouts));
}

static void receive_after(const char *data, int delay_ms) {
    assert(step_count < MAX_STEPS);
    steps[step_count++] = (recv_step_t){data, delay_ms};
}

static void finished(mybot_http_client_response_t *response) {
    mybot_http_client_response_free(response);
    assert(!response->request_may_have_been_sent);
    assert(!connected && allocations == 0 && closes == successful_connects);
    ++tests;
}

static void slow_complete_response(void) {
    reset();
    receive_after(RESPONSE, 6500);
    mybot_http_client_response_t response = {0};
    assert(mybot_http_client_post_timeout_ex(URL, "application/json", "{}", NULL, 30000,
                                             &response) == 0);
    assert(response.status_code == 201 && response.body_len == 2);
    assert(response.request_may_have_been_sent);
    assert(strcmp(response.body, "{}") == 0);
    assert(tick_ms == 7900 && connect_timeout == 30000);
    assert(send_timeouts[0] == 29700 && receive_timeouts[0] == 29600);
    assert(receives == 1); /* Complete Content-Length needs no EOF. */
    finished(&response);
}

static void default_deadlines(void) {
    for (unsigned get = 0; get < 2; ++get) {
        reset();
        receive_after(RESPONSE, 6500);
        mybot_http_client_response_t response = {0};
        int result =
            get ? mybot_http_client_get_ex(URL, NULL, &response)
                : mybot_http_client_post_ex(URL, "application/json", "{}", NULL, &response);
        assert(result < 0 && response.body == NULL);
        assert(response.request_may_have_been_sent);
        assert(connect_timeout == 5000 && receive_timeouts[0] == 4600 && tick_ms == 6000);
        finished(&response);
    }
}

static void fragmented_response_cannot_renew_budget(void) {
    reset();
    connect_delay_ms = send_delay_ms = 1000;
    receive_after("HTTP/1.1 201 Created\r\nContent-Length: 5\r\n\r\na", 7000);
    receive_after("b", 7000);
    receive_after("c", 7000);
    receive_after("de", 8000); /* 31 seconds total cannot fit in the 30-second request. */
    mybot_http_client_response_t response = {0};
    assert(mybot_http_client_post_timeout_ex(URL, "application/json", "{}", NULL, 30000,
                                             &response) < 0);
    assert(tick_ms == 31000 && receives == 4 && response.body == NULL);
    assert(response.request_may_have_been_sent);
    assert(receive_timeouts[0] == 28000 && receive_timeouts[1] == 21000);
    assert(receive_timeouts[2] == 14000 && receive_timeouts[3] == 7000);
    finished(&response);
}

static void short_sends_cannot_renew_budget(void) {
    reset();
    connect_delay_ms = 1000;
    send_delay_ms = 900;
    send_limit = 16;
    mybot_http_client_response_t response = {0};
    assert(mybot_http_client_post_ex(URL, "application/json", "{}", NULL, &response) < 0);
    assert(sends == 5 && receives == 0 && tick_ms == 6000);
    assert(response.request_may_have_been_sent);
    assert(send_timeouts[0] == 4000 && send_timeouts[4] == 400);
    finished(&response);
}

static void truncated_responses_fail(void) {
    reset();
    receive_after("HTTP/1.1 201 Created\r\nContent-Length: 4\r\n\r\nab", 100);
    receive_after(NULL, 100);
    mybot_http_client_response_t response = {0};
    assert(mybot_http_client_post_timeout_ex(URL, "application/json", "{}", NULL, 30000,
                                             &response) < 0);
    assert(response.body == NULL && receives == 2);
    assert(response.request_may_have_been_sent && response.status_code == 201);
    finished(&response);

    reset();
    receive_after("HTTP/1.1 201 Created\r\nContent-Length: 4\r\n\r\nab", 4600);
    assert(mybot_http_client_post_ex(URL, "application/json", "{}", NULL, &response) < 0);
    assert(response.body == NULL && receives == 1 && tick_ms == 6000);
    assert(response.request_may_have_been_sent);
    finished(&response);
}

static void invalid_timeouts_never_connect(void) {
    static const int invalid[] = {0, -1, INT_MIN};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        reset();
        mybot_http_client_response_t response = {.request_may_have_been_sent = true};
        assert(mybot_http_client_post_timeout_ex(URL, "application/json", "{}", NULL, invalid[i],
                                                 &response) < 0);
        assert(connects == 0 && sends == 0 && receives == 0 && tick_ms == 1000);
        assert(!response.request_may_have_been_sent);
        finished(&response);
    }
}

static void failures_before_transport_write_are_not_sent(void) {
    for (unsigned scenario = 0; scenario < 5; ++scenario) {
        reset();
        char oversized[4096];
        memset(oversized, 'x', sizeof(oversized) - 1);
        oversized[sizeof(oversized) - 1] = '\0';
        if (scenario == 0)
            connect_delay_ms = 30001;
        else if (scenario == 1)
            fail_allocation_at = 1; /* HTTP request allocation, after TLS connect. */
        else if (scenario == 4)
            connect_delay_ms = 30000; /* No budget left for the first write. */
        mybot_http_client_response_t response = {.request_may_have_been_sent = true};
        const char *url = scenario == 3 ? "not-a-url" : URL;
        const char *body = scenario == 2 ? oversized : "{}";
        assert(mybot_http_client_post_timeout_ex(url, "application/json", body, NULL, 30000,
                                                 &response) < 0);
        assert(!response.request_may_have_been_sent && !response.status_code && !response.body);
        assert(sends == 0 && receives == 0 && connects <= 1);
        finished(&response);
    }
}

static void failures_after_write_remain_uncertain(void) {
    for (unsigned scenario = 0; scenario < 3; ++scenario) {
        reset();
        if (scenario == 0)
            send_delay_ms = 30001; /* The first attempted write itself fails. */
        else if (scenario == 1)
            fail_allocation_at = 2; /* Read buffer allocation, after the POST. */
        else {
            receive_after(RESPONSE, 100);
            fail_allocation_at = 3; /* Body allocation after parsing HTTP 201. */
        }
        mybot_http_client_response_t response = {0};
        assert(mybot_http_client_post_timeout_ex(URL, "application/json", "{}", NULL, 30000,
                                                 &response) < 0);
        assert(response.request_may_have_been_sent && sends == 1 && connects == 1);
        assert(!response.body);
        if (scenario == 2)
            assert(response.status_code == 201);
        finished(&response);
    }
}

int main(void) {
    slow_complete_response();
    default_deadlines();
    fragmented_response_cannot_renew_budget();
    short_sends_cannot_renew_budget();
    truncated_responses_fail();
    invalid_timeouts_never_connect();
    failures_before_transport_write_are_not_sent();
    failures_after_write_remain_uncertain();
    printf("HTTP deadline: %u production-client transport scenarios passed\n", tests);
    return 0;
}
