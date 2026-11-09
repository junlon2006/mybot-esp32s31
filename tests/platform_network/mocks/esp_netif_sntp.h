/* SPDX-License-Identifier: Apache-2.0 */
#ifndef TEST_ESP_NETIF_SNTP_H
#define TEST_ESP_NETIF_SNTP_H

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/time.h>

typedef void (*esp_sntp_sync_cb_t)(struct timeval *tv);

typedef struct {
    bool start;
    bool wait_for_sync;
    size_t num_of_servers;
    esp_sntp_sync_cb_t sync_cb;
    const char *servers[4];
} esp_sntp_config_t;

#define ESP_SNTP_SERVER_LIST(...) {__VA_ARGS__}
#define ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(count, server_list)                                 \
    {.start = true, .wait_for_sync = true, .num_of_servers = (count), .servers = server_list}

esp_err_t esp_netif_sntp_init(const esp_sntp_config_t *config);
esp_err_t esp_netif_sntp_start(void);
void esp_netif_sntp_deinit(void);

#endif
