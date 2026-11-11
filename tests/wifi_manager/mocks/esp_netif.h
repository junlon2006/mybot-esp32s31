/* SPDX-License-Identifier: MIT */
#ifndef TEST_WIFI_MANAGER_ESP_NETIF_H
#define TEST_WIFI_MANAGER_ESP_NETIF_H
#include "esp_err.h"
typedef struct esp_netif esp_netif_t;
static inline esp_err_t esp_netif_init() {
    return ESP_OK;
}
#endif
