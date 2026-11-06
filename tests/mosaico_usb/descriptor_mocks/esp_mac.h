/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdint.h>
#include "tinyusb.h"

typedef enum { ESP_MAC_EFUSE_FACTORY } esp_mac_type_t;
esp_err_t esp_read_mac(uint8_t *mac, esp_mac_type_t type);
