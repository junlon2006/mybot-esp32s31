/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef void *i2s_chan_handle_t;
typedef struct {
    int id, role, dma_desc_num, dma_frame_num;
    bool auto_clear_after_cb;
} i2s_chan_config_t;
#define I2S_NUM_0 0
#define I2S_ROLE_MASTER 0
#define I2S_CHANNEL_DEFAULT_CONFIG(i, r) ((i2s_chan_config_t){.id = i, .role = r})
#define I2S_DATA_BIT_WIDTH_16BIT 16
#define I2S_SLOT_MODE_STEREO 2
#define I2S_TDM_SLOT0 1
#define I2S_TDM_SLOT1 2
#define I2S_TDM_SLOT2 4
#define I2S_TDM_SLOT3 8
typedef struct {
    int sample_rate_hz;
} host_clk_t;
typedef struct {
    int data_bit_width, slot_mode, slot_mask, total_slot, ws_width;
    bool left_align;
} host_slot_t;
typedef struct {
    int mclk, bclk, ws, dout, din;
} host_gpio_t;
typedef struct {
    host_clk_t clk_cfg;
    host_slot_t slot_cfg;
    host_gpio_t gpio_cfg;
} i2s_tdm_config_t;
#define I2S_TDM_CLK_DEFAULT_CONFIG(rate) ((host_clk_t){.sample_rate_hz = rate})
#define I2S_TDM_PHILIPS_SLOT_DEFAULT_CONFIG(bits, mode, mask)                                      \
    ((host_slot_t){.data_bit_width = bits, .slot_mode = mode, .slot_mask = mask})
esp_err_t i2s_new_channel(const i2s_chan_config_t *, i2s_chan_handle_t *, i2s_chan_handle_t *);
esp_err_t i2s_channel_init_tdm_mode(i2s_chan_handle_t, const i2s_tdm_config_t *);
esp_err_t i2s_channel_enable(i2s_chan_handle_t);
esp_err_t i2s_channel_disable(i2s_chan_handle_t);
esp_err_t i2s_del_channel(i2s_chan_handle_t);
esp_err_t i2s_channel_read(i2s_chan_handle_t, void *, size_t, size_t *, uint32_t);
esp_err_t i2s_channel_write(i2s_chan_handle_t, const void *, size_t, size_t *, uint32_t);
