/* SPDX-License-Identifier: Apache-2.0 */
#include "network/http_time_sync.h"
#include "esp_netif.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);                        \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

#define VALID_RESPONSE                                                                             \
    "HTTP/1.1 200 OK\r\nDate: Tue, 03 Nov 2026 08:09:10 GMT\r\nContent-Length: 2\r\n\r\n{}"
#define VALID_EPOCH INT64_C(1793693350)

static time_t s_now;
static time_t s_set_epoch;
static int s_set_result;
static unsigned int s_set_count;
static unsigned int s_log_count;
static unsigned int s_test_count;
static int s_exec_result;
static unsigned int s_exec_count;
static time_t s_queued_sntp_epoch;

time_t test_time(time_t *destination) {
    if (destination) {
        *destination = s_now;
    }
    return s_now;
}

esp_err_t esp_netif_tcpip_exec(esp_netif_callback_fn callback, void *context) {
    ++s_exec_count;
    if (s_exec_result != ESP_OK) {
        return s_exec_result;
    }
    if (s_queued_sntp_epoch != 0) {
        s_now = s_queued_sntp_epoch;
    }
    return callback(context);
}

int test_settimeofday(const struct timeval *timestamp, const struct timezone *zone) {
    CHECK(timestamp != NULL);
    CHECK(timestamp->tv_usec == 0);
    CHECK(zone == NULL);
    s_set_epoch = timestamp->tv_sec;
    ++s_set_count;
    if (s_set_result == 0) {
        s_now = timestamp->tv_sec;
    }
    return s_set_result;
}

void test_log(const char *format, ...) {
    char message[160];
    va_list arguments;
    va_start(arguments, format);
    int result = vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);
    CHECK(result > 0 && (size_t)result < sizeof(message));
    CHECK(strstr(message, "event=time_sync source=https_date result=ok epoch=") == message);
    ++s_log_count;
}

static void reset(void) {
    s_now = 0;
    s_set_epoch = 0;
    s_set_result = 0;
    s_set_count = 0;
    s_log_count = 0;
    s_exec_result = ESP_OK;
    s_exec_count = 0;
    s_queued_sntp_epoch = 0;
    ++s_test_count;
}

static void feed(mybot_http_time_sync_t *state, const char *text) {
    mybot_http_time_sync_recv(state, text, strlen(text));
}

static void reject(const char *response) {
    reset();
    mybot_http_time_sync_t state = {0};
    feed(&state, response);
    CHECK(s_set_count == 0 && s_log_count == 0);
}

static void expect_date(const char *date, int64_t expected_epoch) {
    char response[256];
    int length = snprintf(response, sizeof(response), "HTTP/1.1 200 OK\r\nDate: %s\r\n\r\n", date);
    CHECK(length > 0 && (size_t)length < sizeof(response));
    reset();
    mybot_http_time_sync_t state = {0};
    feed(&state, response);
    CHECK(s_set_count == 1 && s_log_count == 1);
    CHECK((int64_t)s_set_epoch == expected_epoch);
}

static void reject_date(const char *date) {
    char response[256];
    int length = snprintf(response, sizeof(response), "HTTP/1.1 200 OK\r\nDate: %s\r\n\r\n", date);
    CHECK(length > 0 && (size_t)length < sizeof(response));
    reject(response);
}

static void test_incremental(void) {
    const char response[] = VALID_RESPONSE;
    for (size_t split = 0; split < sizeof(response); ++split) {
        reset();
        mybot_http_time_sync_t state = {0};
        mybot_http_time_sync_recv(&state, response, split);
        mybot_http_time_sync_recv(&state, response + split, sizeof(response) - 1 - split);
        CHECK(state.done && s_set_count == 1 && s_log_count == 1);
        CHECK((int64_t)s_set_epoch == VALID_EPOCH);
        feed(&state, VALID_RESPONSE);
        CHECK(s_set_count == 1);
    }
    reset();
    mybot_http_time_sync_t state = {0};
    for (size_t i = 0; i < sizeof(response) - 1; ++i) {
        mybot_http_time_sync_recv(&state, response + i, 1);
    }
    CHECK(state.done && s_set_count == 1 && (int64_t)s_set_epoch == VALID_EPOCH);

    reset();
    state = (mybot_http_time_sync_t){0};
    feed(&state, "HTTP/1.1 200 OK\r\nDate: Tue, 03 Nov 2026 08:09:10 GMT\r\n");
    CHECK(!state.done && s_set_count == 0);
    feed(&state, "\r\n");
    CHECK(state.done && s_set_count == 1);
}

static void test_independent_connections(void) {
    reset();
    mybot_http_time_sync_t first = {0};
    mybot_http_time_sync_t second = {0};
    feed(&first, "HTTP/1.1 200 OK\r\nDate: Tue, 03 Nov ");
    feed(&second, "HTTP/1.1 200 OK\r\nDate: Thu, 29 Feb 2024 23:59:59 GMT\r\n");
    CHECK(s_set_count == 0);
    feed(&first, "2026 08:09:10 GMT\r\n\r\n");
    CHECK(s_set_count == 1 && (int64_t)s_set_epoch == VALID_EPOCH);
    feed(&second, "\r\n");
    CHECK(second.done && s_set_count == 1);

    reset();
    first = (mybot_http_time_sync_t){0};
    second = (mybot_http_time_sync_t){0};
    feed(&first, "HTTP/1.1 200 OK\r\nDate: INVALID\r\n\r\n");
    feed(&second, VALID_RESPONSE);
    CHECK(first.done && second.done && s_set_count == 1);
}

static void test_dates(void) {
    expect_date("Tue, 03 Nov 2026 08:09:10 GMT", VALID_EPOCH);
    expect_date("Wed, 01 Jan 2020 00:00:00 GMT", INT64_C(1577836800));
    expect_date("Thu, 29 Feb 2024 23:59:59 GMT", INT64_C(1709251199));
    expect_date("Thu, 31 Dec 2099 23:59:59 GMT", INT64_C(4102444799));
    expect_date("Sun, 29 Feb 2032 00:00:00 GMT", INT64_C(1961625600));

    static const char *const invalid[] = {
        "Tue, 31 Dec 2019 23:59:59 GMT",
        "Fri, 01 Jan 2100 00:00:00 GMT",
        "Sun, 29 Feb 2026 00:00:00 GMT",
        "Fri, 29 Feb 2030 00:00:00 GMT",
        "Tue, 31 Nov 2026 00:00:00 GMT",
        "Tue, 00 Nov 2026 00:00:00 GMT",
        "Tue, 32 Nov 2026 00:00:00 GMT",
        "Tue, 03 Nov 2026 24:00:00 GMT",
        "Tue, 03 Nov 2026 08:60:00 GMT",
        "Tue, 03 Nov 2026 08:09:60 GMT",
        "Mon, 03 Nov 2026 08:09:10 GMT",
        "XXX, 03 Nov 2026 08:09:10 GMT",
        "Tue, 03 Xxx 2026 08:09:10 GMT",
        "Tue, 03 Nov 20x6 08:09:10 GMT",
        "Tue, x3 Nov 2026 08:09:10 GMT",
        "Tue, 03 Nov 2026 x8:09:10 GMT",
        "Tue, 03 Nov 2026 08:x9:10 GMT",
        "Tue, 03 Nov 2026 08:09:x0 GMT",
        "Tue, 03 Nov 2026 08:09:10 UTC",
        "Tue, 03 Nov 2026 08:09:10 +0000",
        "Tue, 03 Nov 2026 08:09:10 gmt",
        "tue, 03 Nov 2026 08:09:10 GMT",
        "Tue, 3 Nov 2026 08:09:10 GMT",
        "Tue, 03 Nov 2026 8:09:10 GMT",
        "Tue, 03 Nov 2026 08:09:10 GMT extra",
        "Tuesday, 03-Nov-26 08:09:10 GMT",
        "Tue Nov  3 08:09:10 2026",
        "",
        "Tue, 03 Nov 2026 08:09:10",
    };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        reject_date(invalid[i]);
    }
    CHECK(setenv("TZ", "Pacific/Honolulu", 1) == 0);
    tzset();
    expect_date("Tue, 03 Nov 2026 08:09:10 GMT", VALID_EPOCH);
    CHECK(setenv("TZ", "Asia/Shanghai", 1) == 0);
    tzset();
    expect_date("Tue, 03 Nov 2026 08:09:10 GMT", VALID_EPOCH);
}

static void test_headers(void) {
    static const char *const invalid[] = {
        "HTTP/1.1 200 OK\r\n\r\nDate: Tue, 03 Nov 2026 08:09:10 GMT\r\n\r\n",
        "HTTP/1.1 200 OK\nDate: Tue, 03 Nov 2026 08:09:10 GMT\n\n",
        "HTTP/1.1 200 OK\r\n Date: Tue, 03 Nov 2026 08:09:10 GMT\r\n\r\n",
        "HTTP/1.1 200 OK\r\nDate : Tue, 03 Nov 2026 08:09:10 GMT\r\n\r\n",
        "HTTP/1.1 200 OK\r\n: invalid\r\nDate: Tue, 03 Nov 2026 08:09:10 GMT\r\n\r\n",
        "HTTP/1.1 200 OK\r\nBad Header: invalid\r\nDate: Tue, 03 Nov 2026 08:09:10 GMT\r\n\r\n",
        "HTTP/1.1 200 OK\r\nDate: Tue, 03 Nov 2026 08:09:10 GMT\r\nInvalid\r\n\r\n",
        "HTTP/1.1 200 OK\r\nDate: Tue, 03 Nov 2026 08:09:10 GMT\r\n\tFold\r\n\r\n",
        "HTTP/1.1 200 OK\r\nDate: Tue, 03 Nov 2026 08:09:10 GMT\r\nX: bad\nvalue\r\n\r\n",
        "HTTP/1.1 200 OK\r\nDate: Tue, 03 Nov 2026 08:09:10 GMT\r\nX: bad\rvalue\r\n\r\n",
        "HTTP/1.1 200 OK\r\nDate: Tue, 03 Nov 2026 08:09:10 GMT\r\nX: \177\r\n\r\n",
        "HTTP/1.1 200 OK\r\nDate: Tue, 03 Nov 2026 08:09:10 GMT\r\nX: \001\r\n\r\n",
        "HTTP/1.1 200 OK\r\nDate: Tue, 03 Nov 2026 08:09:10 GMT\r\nDate: INVALID\r\n\r\n",
        "HTTP/1.1 200 OK\r\nDate: Tue, 03 Nov 2026 08:09:10 GMT\r\n"
        "Date: Tue, 03 Nov 2026 08:09:11 GMT\r\n\r\n",
        "HTTP/1.1 200 OK\r\nDate: Tue, 03 Nov 2026 08:09:10 GMT, "
        "Tue, 03 Nov 2026 08:09:10 GMT\r\n\r\n",
    };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        reject(invalid[i]);
    }
    reset();
    mybot_http_time_sync_t state = {0};
    const char with_null[] =
        "HTTP/1.1 200 OK\r\nDate: Tue, 03 Nov 2026 08:09:10 GMT\r\nX: a\0b\r\n\r\n";
    mybot_http_time_sync_recv(&state, with_null, sizeof(with_null) - 1);
    CHECK(state.done && s_set_count == 0);

    reset();
    state = (mybot_http_time_sync_t){0};
    feed(&state, "HTTP/1.0 204 No Content\r\n!#$%&'*+-.^_`|~: accepted\r\n"
                 "dAtE:\tTue, 03 Nov 2026 08:09:10 GMT \t\r\n"
                 "Date: Tue, 03 Nov 2026 08:09:10 GMT\r\n\r\n");
    CHECK(state.done && s_set_count == 1 && (int64_t)s_set_epoch == VALID_EPOCH);
}

static void test_status(void) {
    static const char *const invalid[] = {
        "HTTP/1.1 100 Continue",
        "HTTP/1.1 199 Other",
        "HTTP/1.1 301 Moved",
        "HTTP/1.1 400 Bad",
        "HTTP/1.1 500 Error",
        "HTTP/2 200 OK",
        "HTTP/1.2 200 OK",
        "HTTP/1.1 200",
        "HTTP/1.1 20 OK",
        "HTTP/1.1 2x0 OK",
        "HTTP/1.1 2000 OK",
        "HTTP/1.1  200 OK",
        "HTTP/1.1 200\tOK",
        "HTTP/1.1 200 OK\nBAD",
        "HTTP/1.1 200 \177",
        "",
        "HTTP/1.1",
        "bad",
    };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        char response[256];
        int length = snprintf(response, sizeof(response),
                              "%s\r\nDate: Tue, 03 Nov 2026 08:09:10 GMT\r\n\r\n", invalid[i]);
        CHECK(length > 0 && (size_t)length < sizeof(response));
        reject(response);
    }
    for (int code = 200; code <= 299; ++code) {
        reset();
        mybot_http_time_sync_t state = {0};
        char response[256];
        int length = snprintf(response, sizeof(response),
                              "HTTP/1.1 %d \r\nDate: Tue, 03 Nov 2026 08:09:10 GMT\r\n\r\n", code);
        CHECK(length > 0 && (size_t)length < sizeof(response));
        feed(&state, response);
        CHECK(state.done && s_set_count == 1);
    }
    reject("HTTP/1.1 100 Continue\r\n\r\n" VALID_RESPONSE);
}

static void test_bounds(void) {
    char response[MYBOT_HTTP_TIME_SYNC_HEADER_CAPACITY + 32];
    const char prefix[] = "HTTP/1.1 200 OK\r\nDate: Tue, 03 Nov 2026 08:09:10 GMT\r\nX: ";
    for (size_t header_size = MYBOT_HTTP_TIME_SYNC_HEADER_CAPACITY - 1;
         header_size <= MYBOT_HTTP_TIME_SYNC_HEADER_CAPACITY + 1; ++header_size) {
        reset();
        mybot_http_time_sync_t state = {0};
        memcpy(response, prefix, sizeof(prefix) - 1);
        memset(response + sizeof(prefix) - 1, 'a', header_size - sizeof(prefix) + 1 - 4);
        memcpy(response + header_size - 4, "\r\n\r\n", 4);
        mybot_http_time_sync_recv(&state, response, header_size);
        CHECK(state.length <= MYBOT_HTTP_TIME_SYNC_HEADER_CAPACITY);
        CHECK(state.done);
        CHECK(s_set_count == (header_size <= MYBOT_HTTP_TIME_SYNC_HEADER_CAPACITY ? 1U : 0U));
    }
    reset();
    mybot_http_time_sync_t state = {0};
    memset(response, 'x', sizeof(response));
    mybot_http_time_sync_recv(&state, response, sizeof(response));
    CHECK(state.done && state.length == MYBOT_HTTP_TIME_SYNC_HEADER_CAPACITY && s_set_count == 0);
    feed(&state, VALID_RESPONSE);
    CHECK(s_set_count == 0);

    reset();
    state = (mybot_http_time_sync_t){0};
    mybot_http_time_sync_recv(NULL, response, sizeof(response));
    mybot_http_time_sync_recv(&state, NULL, sizeof(response));
    mybot_http_time_sync_recv(&state, response, 0);
    CHECK(!state.done && state.length == 0 && s_set_count == 0);
}

static void test_clock_gate(void) {
    static const int64_t valid_now[] = {INT64_C(1577836800), VALID_EPOCH, INT64_C(4102444800)};
    for (size_t i = 0; i < sizeof(valid_now) / sizeof(valid_now[0]); ++i) {
        reset();
        mybot_http_time_sync_t state = {0};
        s_now = (time_t)valid_now[i];
        feed(&state, VALID_RESPONSE);
        CHECK(state.done && s_set_count == 0 && s_log_count == 0);
        CHECK((int64_t)s_now == valid_now[i]);
    }
    reset();
    mybot_http_time_sync_t state = {0};
    feed(&state, "HTTP/1.1 200 OK\r\nDate: Tue, 03 Nov 2026 08:09:10 GMT\r\n");
    s_now = (time_t)VALID_EPOCH + 100;
    feed(&state, "\r\n");
    CHECK(state.done && s_set_count == 0);

    reset();
    state = (mybot_http_time_sync_t){0};
    s_now = (time_t)INT64_C(1577836799);
    feed(&state, VALID_RESPONSE);
    CHECK(s_set_count == 1);

    reset();
    state = (mybot_http_time_sync_t){0};
    s_set_result = -1;
    feed(&state, VALID_RESPONSE);
    CHECK(state.done && s_set_count == 1 && s_log_count == 0 && s_now == 0);
    feed(&state, VALID_RESPONSE);
    CHECK(s_set_count == 1);
    mybot_http_time_sync_t next = {0};
    s_set_result = 0;
    feed(&next, VALID_RESPONSE);
    CHECK(s_set_count == 2 && s_log_count == 1 && (int64_t)s_now == VALID_EPOCH);

    reset();
    state = (mybot_http_time_sync_t){0};
    s_queued_sntp_epoch = (time_t)VALID_EPOCH + 100;
    feed(&state, VALID_RESPONSE);
    CHECK(state.done && s_exec_count == 1 && s_set_count == 0 && s_log_count == 0);
    CHECK(s_now == s_queued_sntp_epoch);

    reset();
    state = (mybot_http_time_sync_t){0};
    s_exec_result = ESP_FAIL;
    feed(&state, VALID_RESPONSE);
    CHECK(state.done && s_exec_count == 1 && s_set_count == 0 && s_log_count == 0);
}

int main(void) {
    test_incremental();
    test_independent_connections();
    test_dates();
    test_headers();
    test_status();
    test_bounds();
    test_clock_gate();
    printf("http_time_sync: %u cases passed\n", s_test_count);
    return 0;
}
