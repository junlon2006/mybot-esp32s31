/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2025 Project Contributors */
#include "board_config.h"
#include "korvo_s31_hardware.h"
#include "audio/codec_audio.h"
#include "audio/korvo_s31_codec_audio.h"
#include "driver/i2s_tdm.h"
#include "es8389_codec.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"
#define TAG "korvo_s31_audio"
#define AUDIO_BUS_SLOTS 4
/* Official esp_codec_dev 1.5.9 ES8389 no_dac_ref=false sets serial-TDM in 0x23[7]. */
#define ES8389_ADC_MODE_REG 0x23
#define ES8389_SERIAL_TDM_ENABLE 0x80
#define ES8389_CLOCK_SOURCE_REG 0x02
#define ES8389_ADC_FORMAT_REG 0x20
#define ES8389_ADC_SLOT_REG 0x2A
#define ES8389_DAC_FORMAT_REG 0x40
#define ES8389_DAC_SLOT_REG 0x49
#define ES8389_FORMAT_MASK 0x7C
#define ES8389_FORMAT_PHILIPS_S16 0x60

static int initialize_i2s(i2s_chan_handle_t *tx_channel, i2s_chan_handle_t *rx_channel) {
    i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    channel.dma_desc_num = 6;
    channel.dma_frame_num = 240;
    channel.auto_clear_after_cb = true;
    if (i2s_new_channel(&channel, tx_channel, rx_channel) != ESP_OK)
        return -1;
    /* Four 16-bit time slots provide the 64fs BCLK expected by the official
     * no_dac_ref=false ES8389 clock table. Preserve Philips framing (50% WS,
     * one-bit delay), as selected by that driver. Pin 4 stays unused: the codec
     * derives its clock from BCLK; routing Pin 4 to TDMIN is unnecessary here. */
    i2s_tdm_config_t bus = {
        .clk_cfg = I2S_TDM_CLK_DEFAULT_CONFIG(MYBOT_AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_TDM_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO,
            I2S_TDM_SLOT0 | I2S_TDM_SLOT1 | I2S_TDM_SLOT2 | I2S_TDM_SLOT3),
        .gpio_cfg =
            {
                .mclk = GPIO_NUM_NC,
                .bclk = MYBOT_AUDIO_I2S_BCLK,
                .ws = MYBOT_AUDIO_I2S_WS,
                .dout = MYBOT_AUDIO_I2S_DOUT,
                .din = MYBOT_AUDIO_I2S_DIN,
            },
    };
    bus.slot_cfg.total_slot = AUDIO_BUS_SLOTS;
    bus.slot_cfg.ws_width = 32;
    bus.slot_cfg.left_align = true;
    if (i2s_channel_init_tdm_mode(*tx_channel, &bus) != ESP_OK ||
        i2s_channel_init_tdm_mode(*rx_channel, &bus) != ESP_OK)
        return -1;
    return 0;
}
static int initialize_codec(const audio_codec_ctrl_if_t **ctrl_if,
                            const audio_codec_if_t **codec_if) {
    i2c_master_bus_handle_t bus = korvo_s31_hardware_i2c_bus();
    if (!bus)
        return -1;
    audio_codec_i2c_cfg_t control = {
        .port = I2C_NUM_0,
        .addr = ES8389_CODEC_DEFAULT_ADDR,
        .bus_handle = bus,
    };
    *ctrl_if = audio_codec_new_i2c_ctrl(&control);
    if (!*ctrl_if)
        return -1;
    es8389_codec_cfg_t codec = {
        .ctrl_if = *ctrl_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH,
        .pa_pin = GPIO_NUM_NC,
        .master_mode = false,
        .use_mclk = false,
        .no_dac_ref = false,
        .hw_gain = {.pa_voltage = 5.0f, .codec_dac_voltage = 3.3f},
    };
    *codec_if = es8389_codec_new(&codec);
    if (!*codec_if)
        return -1;
    return 0;
}
static int configure_codec(esp_codec_dev_handle_t device, int volume) {
    int mode = 0, clock = 0, adc_format = 0, dac_format = 0, adc_slot = 0, dac_slot = 0;
    if (esp_codec_dev_read_reg(device, ES8389_ADC_MODE_REG, &mode) != ESP_CODEC_DEV_OK ||
        !(mode & ES8389_SERIAL_TDM_ENABLE) ||
        esp_codec_dev_read_reg(device, ES8389_CLOCK_SOURCE_REG, &clock) != ESP_CODEC_DEV_OK ||
        !(clock & 0x40) ||
        esp_codec_dev_read_reg(device, ES8389_ADC_FORMAT_REG, &adc_format) != ESP_CODEC_DEV_OK ||
        (adc_format & ES8389_FORMAT_MASK) != ES8389_FORMAT_PHILIPS_S16 ||
        esp_codec_dev_read_reg(device, ES8389_DAC_FORMAT_REG, &dac_format) != ESP_CODEC_DEV_OK ||
        (dac_format & ES8389_FORMAT_MASK) != ES8389_FORMAT_PHILIPS_S16 ||
        esp_codec_dev_read_reg(device, ES8389_ADC_SLOT_REG, &adc_slot) != ESP_CODEC_DEV_OK ||
        esp_codec_dev_read_reg(device, ES8389_DAC_SLOT_REG, &dac_slot) != ESP_CODEC_DEV_OK ||
        esp_codec_dev_set_in_gain(device, 30.0f) != ESP_CODEC_DEV_OK ||
        esp_codec_dev_set_out_vol(device, volume) != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "event=audio_device action=start result=error reason=codec_tdm mode=0x%x",
                 mode);
        return -1;
    }
    /* ADC serial-TDM mode is verified separately from the DAC framing. The
     * public codec API does not program a DAC TDM slot layout from channel=4.
     * Native slot order and both physical directions require board validation. */
    ESP_LOGI(TAG,
             "event=audio_bus action=start rate=%d controller=tdm slots=4 bits=16 "
             "bclk_hz=%d ws_high_bclk=32 framing=philips tx_slots=mono_copy rx_slot=0 "
             "clock_reg02=0x%02x adc_reg20=0x%02x adc_reg23=0x%02x adc_reg2a=0x%02x "
             "dac_reg40=0x%02x dac_reg49=0x%02x hardware_validation=pending",
             MYBOT_AUDIO_SAMPLE_RATE, MYBOT_AUDIO_SAMPLE_RATE * 64, clock, adc_format, mode,
             adc_slot, dac_format, dac_slot);
    return 0;
}

const mybot_codec_audio_board_t mybot_codec_audio_board = {
    .tag = TAG,
    .bus_slots = AUDIO_BUS_SLOTS,
    .initialize_i2s = initialize_i2s,
    .initialize_codec = initialize_codec,
    .configure_codec = configure_codec,
};
const mybot_audio_capture_ops_t *mybot_korvo_s31_audio_capture_ops(void) {
    return mybot_codec_audio_capture_ops();
}
const mybot_audio_playback_ops_t *mybot_korvo_s31_audio_playback_ops(void) {
    return mybot_codec_audio_playback_ops();
}
const mybot_audio_volume_ops_t *mybot_korvo_s31_audio_volume_ops(void) {
    return mybot_codec_audio_volume_ops();
}
