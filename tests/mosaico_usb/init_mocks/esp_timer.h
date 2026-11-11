/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_TEST_MOSAICO_USB_INIT_MOCKS_ESP_TIMER_H_
#define MYBOT_TEST_MOSAICO_USB_INIT_MOCKS_ESP_TIMER_H_

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define ESP_TIMER_TASK 1
typedef void *esp_timer_handle_t;
typedef struct {
    void (*callback)(void *);
    void *arg;
    int dispatch_method;
    const char *name;
    bool skip_unhandled_events;
} esp_timer_create_args_t;

esp_err_t esp_timer_create(const esp_timer_create_args_t *config, esp_timer_handle_t *timer);
esp_err_t esp_timer_start_periodic(esp_timer_handle_t timer, uint64_t period);
esp_err_t esp_timer_delete(esp_timer_handle_t timer);

#endif /* MYBOT_TEST_MOSAICO_USB_INIT_MOCKS_ESP_TIMER_H_ */
