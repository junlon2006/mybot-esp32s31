/* SPDX-License-Identifier: Apache-2.0 */
#include "network/conversation_http_trace.h"

#include "esp_heap_caps.h"
#include "esp_log.h"

#include <stdint.h>
#include <string.h>

#define TAG "conversation_http"
/* Match the SDK's existing wire limits without depending on private headers. */
#define REQUEST_CAPACITY 2048U
#define RESPONSE_CAPACITY (32U * 1024U)
#define LOG_CHUNK_SIZE 256U
#define TRACE_MEMORY_CAPS (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)

struct mybot_conversation_http_trace {
    unsigned int id;
    size_t request_size;
    size_t response_size;
    int selected; /* 0: request line incomplete, 1: selected, -1: other request */
    bool request_overflow;
    bool response_overflow;
    bool response_no_memory;
    char *response;
    char request[REQUEST_CAPACITY + 1U];
};

typedef struct {
    size_t body_offset;
    size_t content_length;
    int status;
    bool has_length;
    bool chunked;
    bool valid;
} http_headers_t;

static unsigned int s_next_id;

static bool equal_ascii(const char *data, size_t size, const char *expected) {
    if (strlen(expected) != size) {
        return false;
    }
    for (size_t i = 0; i < size; ++i) {
        unsigned char c = (unsigned char)data[i];
        if (c >= 'A' && c <= 'Z') {
            c += 'a' - 'A';
        }
        if (c != (unsigned char)expected[i]) {
            return false;
        }
    }
    return true;
}

static const char *line_end(const char *data, const char *end) {
    while (data + 1 < end) {
        if (data[0] == '\r' && data[1] == '\n') {
            return data;
        }
        ++data;
    }
    return NULL;
}

static int select_request(const char *request, size_t size) {
    const char *end = line_end(request, request + size);
    if (!end) {
        return 0;
    }
    if (end - request < 5 || memcmp(request, "POST ", 5) != 0) {
        return -1;
    }
    const char *path = request + 5;
    const char *path_end = memchr(path, ' ', (size_t)(end - path));
    if (!path_end) {
        return -1;
    }
    const char *query = memchr(path, '?', (size_t)(path_end - path));
    if (query) {
        path_end = query;
    }
    static const char suffix[] = "/conversations/start";
    size_t suffix_size = sizeof(suffix) - 1U;
    return (size_t)(path_end - path) >= suffix_size &&
                   memcmp(path_end - suffix_size, suffix, suffix_size) == 0
               ? 1
               : -1;
}

static bool parse_length(const char *value, const char *end, size_t *length) {
    while (value < end && (*value == ' ' || *value == '\t')) {
        ++value;
    }
    bool digit = false;
    size_t parsed = 0;
    while (value < end && *value >= '0' && *value <= '9') {
        unsigned int n = (unsigned int)(*value++ - '0');
        if (parsed > (SIZE_MAX - n) / 10U) {
            return false;
        }
        parsed = parsed * 10U + n;
        digit = true;
    }
    while (value < end && (*value == ' ' || *value == '\t')) {
        ++value;
    }
    if (!digit || value != end) {
        return false;
    }
    *length = parsed;
    return true;
}

static bool is_chunked(const char *value, const char *end) {
    while (value < end) {
        while (value < end && (*value == ' ' || *value == '\t' || *value == ',')) {
            ++value;
        }
        const char *token = value;
        while (value < end && *value != ',' && *value != ';' && *value != ' ' && *value != '\t') {
            ++value;
        }
        if (equal_ascii(token, (size_t)(value - token), "chunked")) {
            return true;
        }
        while (value < end && *value != ',') {
            ++value;
        }
    }
    return false;
}

static http_headers_t parse_headers(const char *data, size_t size, bool response) {
    http_headers_t headers = {0};
    const char *end = data + size;
    const char *first_end = line_end(data, end);
    if (!first_end) {
        return headers;
    }
    if (response) {
        if (first_end - data < 12 || memcmp(data, "HTTP/1.", 7) != 0 || data[8] != ' ' ||
            data[9] < '0' || data[9] > '9' || data[10] < '0' || data[10] > '9' || data[11] < '0' ||
            data[11] > '9') {
            return headers;
        }
        headers.status = (data[9] - '0') * 100 + (data[10] - '0') * 10 + data[11] - '0';
    }
    const char *line = first_end + 2;
    while (line < end) {
        const char *last = line_end(line, end);
        if (!last) {
            return headers;
        }
        if (last == line) {
            headers.body_offset = (size_t)(last + 2 - data);
            headers.valid = true;
            return headers;
        }
        const char *colon = memchr(line, ':', (size_t)(last - line));
        if (!colon) {
            return headers;
        }
        if (equal_ascii(line, (size_t)(colon - line), "content-length")) {
            size_t length;
            if (!parse_length(colon + 1, last, &length) ||
                (headers.has_length && headers.content_length != length)) {
                return headers;
            }
            headers.has_length = true;
            headers.content_length = length;
        } else if (equal_ascii(line, (size_t)(colon - line), "transfer-encoding")) {
            headers.chunked |= is_chunked(colon + 1, last);
        }
        line = last + 2;
    }
    return headers;
}

/* Compact chunk payload in place. Keep even a partial final payload for diagnosis;
 * chunk sizes, extensions and trailers are never included in body logs. */
static size_t decode_chunks(char *data, size_t size, bool *complete) {
    char *cursor = data;
    char *end = data + size;
    size_t output = 0;
    *complete = false;
    while (cursor < end) {
        const char *last = line_end(cursor, end);
        if (!last) {
            break;
        }
        const char *number = cursor;
        size_t chunk_size = 0;
        bool digit = false;
        while (number < last && *number != ';' && *number != ' ' && *number != '\t') {
            unsigned char c = (unsigned char)*number++;
            unsigned int n;
            if (c >= '0' && c <= '9') {
                n = c - '0';
            } else if (c >= 'a' && c <= 'f') {
                n = c - 'a' + 10U;
            } else if (c >= 'A' && c <= 'F') {
                n = c - 'A' + 10U;
            } else {
                return output;
            }
            if (chunk_size > (SIZE_MAX - n) / 16U) {
                return output;
            }
            chunk_size = chunk_size * 16U + n;
            digit = true;
        }
        if (!digit) {
            break;
        }
        cursor = (char *)last + 2;
        if (chunk_size == 0) {
            /* Ignore trailer fields, including any private header values. */
            while (cursor < end) {
                last = line_end(cursor, end);
                if (!last) {
                    return output;
                }
                if (last == cursor) {
                    *complete = true;
                    return output;
                }
                cursor = (char *)last + 2;
            }
            break;
        }
        size_t available = (size_t)(end - cursor);
        size_t copied = chunk_size < available ? chunk_size : available;
        memmove(data + output, cursor, copied);
        output += copied;
        cursor += copied;
        if (copied < chunk_size || end - cursor < 2 || cursor[0] != '\r' || cursor[1] != '\n') {
            break;
        }
        cursor += 2;
    }
    return output;
}

static void log_body(unsigned int id, const char *direction, const char *body, size_t size,
                     bool complete, int status, const char *reason) {
    bool binary = size > 0 && memchr(body, '\0', size) != NULL;
    ESP_LOGI(TAG,
             "event=http_body id=%u direction=%s phase=begin bytes=%zu complete=%s status=%d "
             "reason=%s",
             id, direction, size, complete ? "yes" : "no", status, reason);
    for (size_t offset = 0; offset < size;) {
        size_t length = size - offset;
        if (length > LOG_CHUNK_SIZE) {
            length = LOG_CHUNK_SIZE;
            /* Keep valid UTF-8 code points together across log records. */
            while (!binary && length > 0 &&
                   ((unsigned char)body[offset + length] & 0xc0U) == 0x80U) {
                --length;
            }
            if (length == 0) {
                length = LOG_CHUNK_SIZE;
            }
        }
        if (binary) {
            static const char hex[] = "0123456789abcdef";
            char encoded[LOG_CHUNK_SIZE * 2U + 1U];
            for (size_t i = 0; i < length; ++i) {
                unsigned char byte = (unsigned char)body[offset + i];
                encoded[i * 2U] = hex[byte >> 4U];
                encoded[i * 2U + 1U] = hex[byte & 0xfU];
            }
            encoded[length * 2U] = '\0';
            ESP_LOGI(
                TAG,
                "event=http_body id=%u direction=%s phase=data offset=%zu encoding=hex data=%s", id,
                direction, offset, encoded);
        } else {
            ESP_LOGI(TAG, "event=http_body id=%u direction=%s phase=data offset=%zu data=%.*s", id,
                     direction, offset, (int)length, body + offset);
        }
        offset += length;
    }
    ESP_LOGI(TAG, "event=http_body id=%u direction=%s phase=end bytes=%zu complete=%s", id,
             direction, size, complete ? "yes" : "no");
}

mybot_conversation_http_trace_t *mybot_conversation_http_trace_create(void) {
    mybot_conversation_http_trace_t *trace = heap_caps_calloc(1, sizeof(*trace), TRACE_MEMORY_CAPS);
    if (!trace) {
        ESP_LOGW(TAG, "event=http_body_trace result=unavailable reason=no_memory");
        return NULL;
    }
    trace->id = __atomic_add_fetch(&s_next_id, 1U, __ATOMIC_RELAXED);
    return trace;
}

void mybot_conversation_http_trace_send(mybot_conversation_http_trace_t *trace, const void *data,
                                        size_t size) {
    if (!trace || !data || size == 0 || trace->selected < 0) {
        return;
    }
    size_t available = REQUEST_CAPACITY - trace->request_size;
    size_t copied = size < available ? size : available;
    memcpy(trace->request + trace->request_size, data, copied);
    trace->request_size += copied;
    trace->request[trace->request_size] = '\0';
    trace->request_overflow |= copied != size;
    if (!trace->selected) {
        trace->selected = select_request(trace->request, trace->request_size);
    }
}

void mybot_conversation_http_trace_recv(mybot_conversation_http_trace_t *trace, const void *data,
                                        size_t size) {
    if (!trace || trace->selected != 1 || !data || size == 0 || trace->response_no_memory) {
        return;
    }
    if (!trace->response) {
        trace->response = heap_caps_malloc(RESPONSE_CAPACITY + 1U, TRACE_MEMORY_CAPS);
        if (!trace->response) {
            trace->response_no_memory = true;
            return;
        }
    }
    size_t available = RESPONSE_CAPACITY - trace->response_size;
    size_t copied = size < available ? size : available;
    memcpy(trace->response + trace->response_size, data, copied);
    trace->response_size += copied;
    trace->response[trace->response_size] = '\0';
    trace->response_overflow |= copied != size;
}

void mybot_conversation_http_trace_finish(mybot_conversation_http_trace_t *trace, bool eof,
                                          bool transport_error) {
    if (!trace) {
        return;
    }
    if (trace->selected == 1) {
        http_headers_t request = parse_headers(trace->request, trace->request_size, false);
        size_t size = request.valid ? trace->request_size - request.body_offset : 0;
        if (request.has_length && size > request.content_length) {
            size = request.content_length;
        }
        bool complete = request.valid && request.has_length && size == request.content_length &&
                        !trace->request_overflow;
        const char *reason = trace->request_overflow ? "capacity"
                             : request.valid         ? "sent"
                                                     : "headers";
        log_body(trace->id, "request", trace->request + request.body_offset, size, complete, 0,
                 reason);

        http_headers_t response = {0};
        if (trace->response) {
            response = parse_headers(trace->response, trace->response_size, true);
        }
        size = response.valid ? trace->response_size - response.body_offset : 0;
        complete = false;
        char *body = trace->response ? trace->response + response.body_offset : NULL;
        if (response.valid && response.chunked) {
            size = decode_chunks(body, size, &complete);
        } else if (response.valid && response.has_length) {
            if (size > response.content_length) {
                size = response.content_length;
            }
            complete = size == response.content_length;
        } else if (response.valid) {
            complete = eof && !transport_error;
        }
        complete &= !trace->response_overflow && !trace->response_no_memory;
        reason = trace->response_no_memory  ? "no_memory"
                 : trace->response_overflow ? "capacity"
                 : !response.valid          ? "headers"
                 : complete                 ? "received"
                 : transport_error          ? "transport_error"
                 : !eof                     ? "not_finished"
                                            : "early_eof";
        log_body(trace->id, "response", body, size, complete, response.status, reason);
    }
    heap_caps_free(trace->response);
    heap_caps_free(trace);
}
