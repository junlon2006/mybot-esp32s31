/* SPDX-License-Identifier: Apache-2.0 */
#include <mybot/platform/mybot_https.h>

#include "network/conversation_http_trace.h"

#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_tls.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#define TAG "mybot_https"

typedef struct {
    esp_tls_t *tls;
    int socket_fd;
    mybot_conversation_http_trace_t *trace;
    bool peer_closed;
    bool io_error;
} https_connection_t;

static int set_socket_timeout(https_connection_t *connection, int option, int timeout_ms) {
    struct timeval timeout = {
        .tv_sec = timeout_ms / 1000,
        .tv_usec = (timeout_ms % 1000) * 1000,
    };
    return setsockopt(connection->socket_fd, SOL_SOCKET, option, &timeout, sizeof(timeout));
}

static int https_connect(void **out_connection, const char *host, uint16_t port, int timeout_ms) {
    if (!out_connection || !host || !host[0] || port == 0 || timeout_ms <= 0) {
        return -1;
    }
    *out_connection = NULL;

    https_connection_t *connection =
        heap_caps_calloc(1, sizeof(*connection), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!connection) {
        return -1;
    }
    connection->socket_fd = -1;
    connection->tls = esp_tls_init();
    if (!connection->tls) {
        free(connection);
        return -1;
    }

    esp_tls_cfg_t config = {
        .timeout_ms = timeout_ms,
        .common_name = host,
        .skip_common_name = false,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    int result = esp_tls_conn_new_sync(host, (int)strlen(host), port, &config, connection->tls);
    if (result != 1 || esp_tls_get_conn_sockfd(connection->tls, &connection->socket_fd) != ESP_OK) {
        ESP_LOGE(TAG, "TLS connection to %s:%u failed", host, (unsigned int)port);
        esp_tls_conn_destroy(connection->tls);
        free(connection);
        return -1;
    }

    connection->trace = mybot_conversation_http_trace_create();
    *out_connection = connection;
    return 0;
}

static int https_send(void *opaque, const void *data, size_t len, int timeout_ms) {
    https_connection_t *connection = opaque;
    if (!connection || !connection->tls || !data || len == 0 || timeout_ms <= 0) {
        return -1;
    }
    if (set_socket_timeout(connection, SO_SNDTIMEO, timeout_ms) < 0) {
        connection->io_error = true;
        return -1;
    }

    ssize_t result = esp_tls_conn_write(connection->tls, data, len);
    if (result > 0 && result <= INT_MAX) {
        mybot_conversation_http_trace_send(connection->trace, data, (size_t)result);
        return (int)result;
    }
    connection->io_error = true;
    return -1;
}

static int https_recv(void *opaque, void *data, size_t capacity, int timeout_ms) {
    https_connection_t *connection = opaque;
    if (!connection || !connection->tls || !data || capacity == 0 || timeout_ms <= 0) {
        return -1;
    }
    if (set_socket_timeout(connection, SO_RCVTIMEO, timeout_ms) < 0) {
        connection->io_error = true;
        return -1;
    }

    ssize_t result = esp_tls_conn_read(connection->tls, data, capacity);
    if (result == 0) {
        connection->peer_closed = true;
        return 0;
    }
    if (result > 0 && result <= INT_MAX) {
        mybot_conversation_http_trace_recv(connection->trace, data, (size_t)result);
        return (int)result;
    }
    connection->io_error = true;
    return -1;
}

static void https_close(void *opaque) {
    https_connection_t *connection = opaque;
    if (!connection) {
        return;
    }
    if (connection->tls) {
        esp_tls_conn_destroy(connection->tls);
    }
    mybot_conversation_http_trace_finish(connection->trace, connection->peer_closed,
                                         connection->io_error);
    free(connection);
}

static const mybot_https_ops_t s_ops = {
    .connect = https_connect,
    .send = https_send,
    .recv = https_recv,
    .close = https_close,
};

const mybot_https_ops_t *mybot_esp_idf_https_ops(void) {
    return &s_ops;
}
