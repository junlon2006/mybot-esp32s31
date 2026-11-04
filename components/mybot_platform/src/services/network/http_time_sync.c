/* SPDX-License-Identifier: Apache-2.0 */
#include "network/http_time_sync.h"

#include "esp_log.h"
#include "esp_netif.h"

#include <stdint.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#define TAG "mybot_time"
#define MIN_VALID_EPOCH INT64_C(1577836800) /* 2020-01-01 00:00:00 UTC */

static bool is_digit(char value) {
    return value >= '0' && value <= '9';
}

static int decimal_pair(const char *text) {
    if (!is_digit(text[0]) || !is_digit(text[1])) {
        return -1;
    }
    return (text[0] - '0') * 10 + text[1] - '0';
}

static bool leap_year(int year) {
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

static bool parse_date(const char *text, size_t length, int64_t *epoch) {
    static const char *const weekdays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char *const months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                         "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    static const int month_days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (length != 29 || text[3] != ',' || text[4] != ' ' || text[7] != ' ' || text[11] != ' ' ||
        text[16] != ' ' || text[19] != ':' || text[22] != ':' ||
        memcmp(text + 25, " GMT", 4) != 0) {
        return false;
    }
    int weekday = -1;
    int month = -1;
    for (int i = 0; i < 7; ++i) {
        if (memcmp(text, weekdays[i], 3) == 0) {
            weekday = i;
        }
    }
    for (int i = 0; i < 12; ++i) {
        if (memcmp(text + 8, months[i], 3) == 0) {
            month = i;
        }
    }
    int day = decimal_pair(text + 5);
    int century = decimal_pair(text + 12);
    int year_part = decimal_pair(text + 14);
    int hour = decimal_pair(text + 17);
    int minute = decimal_pair(text + 20);
    int second = decimal_pair(text + 23);
    if (century < 0 || year_part < 0) {
        return false;
    }
    int year = century * 100 + year_part;
    if (weekday < 0 || month < 0 || year < 2020 || year >= 2100 || day < 1 || hour < 0 ||
        hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59) {
        return false;
    }
    int days_in_month = month_days[month] + (month == 1 && leap_year(year) ? 1 : 0);
    if (day > days_in_month) {
        return false;
    }
    int64_t days = 0;
    for (int i = 1970; i < year; ++i) {
        days += leap_year(i) ? 366 : 365;
    }
    for (int i = 0; i < month; ++i) {
        days += month_days[i] + (i == 1 && leap_year(year) ? 1 : 0);
    }
    days += day - 1;
    if ((days + 4) % 7 != weekday) {
        return false;
    }
    *epoch = ((days * 24 + hour) * 60 + minute) * 60 + second;
    return true;
}

static bool field_name_character(unsigned char value) {
    return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
           (value >= '0' && value <= '9') ||
           (value != 0 && strchr("!#$%&'*+-.^_`|~", value) != NULL);
}

static bool field_value_character(unsigned char value) {
    return value == '\t' || (value >= 0x20 && value != 0x7f);
}

static bool is_date_field(const char *text, size_t length) {
    return length == 4 && (text[0] == 'd' || text[0] == 'D') &&
           (text[1] == 'a' || text[1] == 'A') && (text[2] == 't' || text[2] == 'T') &&
           (text[3] == 'e' || text[3] == 'E');
}

static size_t line_end(const char *header, size_t length, size_t offset) {
    for (size_t i = offset; i + 1 < length; ++i) {
        if (header[i] == '\r' && header[i + 1] == '\n') {
            return i;
        }
    }
    return length;
}

static bool parse_header(const char *header, size_t length, int64_t *epoch) {
    size_t end = line_end(header, length, 0);
    if (end < 13 || end == length ||
        (memcmp(header, "HTTP/1.1 ", 9) != 0 && memcmp(header, "HTTP/1.0 ", 9) != 0) ||
        header[9] != '2' || !is_digit(header[10]) || !is_digit(header[11]) || header[12] != ' ') {
        return false;
    }
    for (size_t i = 13; i < end; ++i) {
        if (!field_value_character((unsigned char)header[i])) {
            return false;
        }
    }

    bool have_date = false;
    size_t offset = end + 2;
    while (offset < length) {
        end = line_end(header, length, offset);
        if (end == length) {
            return false;
        }
        if (end == offset) {
            return end + 2 == length && have_date;
        }
        size_t colon = offset;
        while (colon < end && field_name_character((unsigned char)header[colon])) {
            ++colon;
        }
        if (colon == offset || colon == end || header[colon] != ':') {
            return false;
        }
        for (size_t i = colon + 1; i < end; ++i) {
            if (!field_value_character((unsigned char)header[i])) {
                return false;
            }
        }
        if (is_date_field(header + offset, colon - offset)) {
            size_t start = colon + 1;
            size_t finish = end;
            while (start < finish && (header[start] == ' ' || header[start] == '\t')) {
                ++start;
            }
            while (finish > start && (header[finish - 1] == ' ' || header[finish - 1] == '\t')) {
                --finish;
            }
            int64_t parsed_epoch;
            if (!parse_date(header + start, finish - start, &parsed_epoch) ||
                (have_date && *epoch != parsed_epoch)) {
                return false;
            }
            *epoch = parsed_epoch;
            have_date = true;
        }
        offset = end + 2;
    }
    return false;
}

/* SNTP updates the clock in the lwIP TCP/IP context too. Check and set there as
 * one operation so an SNTP reply queued before this callback cannot be overwritten. */
static esp_err_t bootstrap_time(void *context) {
    const int64_t epoch = *(const int64_t *)context;
    if ((int64_t)time(NULL) >= MIN_VALID_EPOCH) {
        return ESP_OK;
    }
    struct timeval timestamp = {.tv_sec = (time_t)epoch, .tv_usec = 0};
    if (settimeofday(&timestamp, NULL) != 0) {
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "event=time_sync source=https_date result=ok epoch=%lld", (long long)epoch);
    return ESP_OK;
}

void mybot_http_time_sync_recv(mybot_http_time_sync_t *state, const void *data, size_t length) {
    if (!state || state->done || !data || length == 0) {
        return;
    }
    const char *bytes = data;
    for (size_t i = 0; i < length; ++i) {
        if (state->length == sizeof(state->header)) {
            state->done = true;
            return;
        }
        state->header[state->length++] = bytes[i];
        size_t size = state->length;
        if (size < 4 || memcmp(state->header + size - 4, "\r\n\r\n", 4) != 0) {
            continue;
        }
        state->done = true;
        int64_t epoch = 0;
        if (!parse_header(state->header, size, &epoch)) {
            return;
        }
        /* This API waits for completion, so the stack context remains valid.
         * Failure is deliberately independent of the HTTPS receive result. */
        (void)esp_netif_tcpip_exec(bootstrap_time, &epoch);
        return;
    }
}
