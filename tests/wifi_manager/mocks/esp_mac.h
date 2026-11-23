/* SPDX-License-Identifier: MIT */
#ifndef TEST_WIFI_MANAGER_ESP_MAC_H
#define TEST_WIFI_MANAGER_ESP_MAC_H
#include <cstdint>
#include "esp_err.h"
#define ESP_MAC_WIFI_STA 0
extern uint8_t test_wifi_mac[6];
extern esp_err_t test_wifi_mac_error;
static inline esp_err_t esp_read_mac(uint8_t *mac, int type) {
    if (type != ESP_MAC_WIFI_STA || test_wifi_mac_error != ESP_OK)
        return ESP_ERR_INVALID_STATE;
    for (int i = 0; i < 6; ++i)
        mac[i] = test_wifi_mac[i];
    return ESP_OK;
}
#endif
