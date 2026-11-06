/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include "esp_err.h"

#define TINYUSB_CDC_ACM_0 0
typedef struct {
    int cdc_port;
} tinyusb_config_cdcacm_t;

esp_err_t tinyusb_cdcacm_init(const tinyusb_config_cdcacm_t *config);
