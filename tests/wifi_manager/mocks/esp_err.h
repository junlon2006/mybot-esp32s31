/* SPDX-License-Identifier: MIT */
#ifndef TEST_WIFI_MANAGER_ESP_ERR_H
#define TEST_WIFI_MANAGER_ESP_ERR_H

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 1
#define ESP_ERR_NVS_NO_FREE_PAGES 2
#define ESP_ERR_NVS_NEW_VERSION_FOUND 3
#define ESP_ERROR_CHECK(expression) ((void)(expression))
static inline const char *esp_err_to_name(esp_err_t error) {
    (void)error;
    return "mock error";
}

#endif
