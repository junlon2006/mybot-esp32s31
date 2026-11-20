/* SPDX-License-Identifier: MIT */
#ifndef TEST_WIFI_MANAGER_ESP_WIFI_TYPES_H
#define TEST_WIFI_MANAGER_ESP_WIFI_TYPES_H
#include <cstdint>
typedef int wifi_auth_mode_t;
typedef struct {
    uint8_t ssid[32];
    uint8_t password[64];
} wifi_sta_config_t;
typedef struct {
    uint8_t ssid[33];
    int8_t rssi;
    wifi_auth_mode_t authmode;
} wifi_ap_record_t;
#endif
