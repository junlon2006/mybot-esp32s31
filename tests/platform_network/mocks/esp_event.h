/* SPDX-License-Identifier: Apache-2.0 */
#ifndef TEST_ESP_EVENT_H
#define TEST_ESP_EVENT_H

#include "esp_err.h"

#include <stdint.h>

typedef const char *esp_event_base_t;
typedef void *esp_event_handler_instance_t;
typedef void (*esp_event_handler_t)(void *arg, esp_event_base_t base, int32_t id, void *event_data);

extern esp_event_base_t IP_EVENT;
enum { IP_EVENT_STA_GOT_IP = 1 };

esp_err_t esp_event_loop_create_default(void);
esp_err_t esp_event_handler_instance_register(esp_event_base_t base, int32_t id,
                                              esp_event_handler_t handler, void *arg,
                                              esp_event_handler_instance_t *instance);
esp_err_t esp_event_handler_instance_unregister(esp_event_base_t base, int32_t id,
                                                esp_event_handler_instance_t instance);

#endif
