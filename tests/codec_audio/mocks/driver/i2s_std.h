/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "driver/i2s_common.h"
typedef struct {
    int sample_rate_hz, mclk_multiple;
} host_std_clk_t;
typedef struct {
    host_std_clk_t clk_cfg;
    host_slot_t slot_cfg;
    host_gpio_t gpio_cfg;
} i2s_std_config_t;
#define I2S_STD_CLK_DEFAULT_CONFIG(rate)                                                           \
    ((host_std_clk_t){.sample_rate_hz = rate, .mclk_multiple = 256})
#define I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(bits, mode)                                            \
    ((host_slot_t){.data_bit_width = bits, .slot_mode = mode})
esp_err_t i2s_channel_init_std_mode(i2s_chan_handle_t, const i2s_std_config_t *);
