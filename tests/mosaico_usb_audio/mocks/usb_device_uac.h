/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
typedef struct {
    bool skip_tinyusb_init;
    esp_err_t (*output_cb)(uint8_t *, size_t, void *);
    esp_err_t (*input_cb)(uint8_t *, size_t, size_t *, void *);
    void (*set_mute_cb)(uint32_t, void *);
    void (*set_volume_cb)(uint32_t, void *);
    void *cb_ctx;
    int spk_itf_num, mic_itf_num;
} uac_device_config_t;
esp_err_t uac_device_init(uac_device_config_t *config);
