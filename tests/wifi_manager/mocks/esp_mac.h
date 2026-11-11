/* SPDX-License-Identifier: MIT */
#ifndef TEST_WIFI_MANAGER_ESP_MAC_H
#define TEST_WIFI_MANAGER_ESP_MAC_H
#include <cstdint>
#include "esp_err.h"
#define ESP_MAC_WIFI_STA 0
static inline esp_err_t esp_read_mac(uint8_t *mac, int) {
    for (int i = 0; i < 6; ++i)
        mac[i] = static_cast<uint8_t>(i);
    return ESP_OK;
}
#endif
