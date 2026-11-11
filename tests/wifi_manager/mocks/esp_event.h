/* SPDX-License-Identifier: MIT */
#ifndef TEST_WIFI_MANAGER_ESP_EVENT_H
#define TEST_WIFI_MANAGER_ESP_EVENT_H

#include <cstdint>
#include "esp_err.h"

typedef void *EventGroupHandle_t;
typedef void *esp_event_handler_instance_t;
typedef const char *esp_event_base_t;
static inline esp_err_t esp_event_loop_create_default() {
    return ESP_OK;
}

#endif
