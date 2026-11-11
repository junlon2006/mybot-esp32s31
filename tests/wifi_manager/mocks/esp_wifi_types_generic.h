/* SPDX-License-Identifier: MIT */
#ifndef TEST_WIFI_MANAGER_ESP_WIFI_TYPES_H
#define TEST_WIFI_MANAGER_ESP_WIFI_TYPES_H
#include <cstdint>
typedef int wifi_auth_mode_t;
typedef struct {
    uint8_t ssid[33];
} wifi_ap_record_t;
#endif
