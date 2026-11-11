/* SPDX-License-Identifier: MIT */
#ifndef TEST_WIFI_MANAGER_ESP_WIFI_H
#define TEST_WIFI_MANAGER_ESP_WIFI_H
#include "esp_err.h"
typedef struct {
    bool nvs_enable;
} wifi_init_config_t;
#define WIFI_INIT_CONFIG_DEFAULT()                                                                 \
    wifi_init_config_t {                                                                           \
        true                                                                                       \
    }
static inline esp_err_t esp_wifi_init(const wifi_init_config_t *) {
    return ESP_OK;
}
static inline esp_err_t esp_wifi_deinit() {
    return ESP_OK;
}
#endif
