/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_HTTP_TIME_SYNC_H
#define MYBOT_HTTP_TIME_SYNC_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MYBOT_HTTP_TIME_SYNC_HEADER_CAPACITY 2048U

/* Zero-initialize one state per verified TLS connection. Keep it in PSRAM with
 * its connection; no heap allocation or response body storage is needed here. */
typedef struct {
    char header[MYBOT_HTTP_TIME_SYNC_HEADER_CAPACITY];
    size_t length;
    bool done;
} mybot_http_time_sync_t;

/* Feed only bytes actually received through the verified TLS connection. This
 * observes the first response header and never changes HTTP transport results. */
void mybot_http_time_sync_recv(mybot_http_time_sync_t *state, const void *data, size_t length);

#ifdef __cplusplus
}
#endif

#endif /* MYBOT_HTTP_TIME_SYNC_H */
