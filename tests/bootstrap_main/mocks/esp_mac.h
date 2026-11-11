/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_TEST_BOOTSTRAP_MAIN_MOCKS_ESP_MAC_H_
#define MYBOT_TEST_BOOTSTRAP_MAIN_MOCKS_ESP_MAC_H_

#include "esp_err.h"
#include <stdint.h>

typedef enum { ESP_MAC_WIFI_STA } esp_mac_type_t;

esp_err_t esp_read_mac(uint8_t *mac, esp_mac_type_t type);

#endif /* MYBOT_TEST_BOOTSTRAP_MAIN_MOCKS_ESP_MAC_H_ */
