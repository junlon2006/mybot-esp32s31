/* SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD */
/* SPDX-FileCopyrightText: 2026 MyBot Project Contributors */
/* SPDX-License-Identifier: Apache-2.0 */
#include "board_config.h"
#include "mosaico_hardware.h"
#include "audio/codec_audio.h"
#include "audio/mosaico_codec_audio.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev_defaults.h"
#include "es8311_codec.h"
#include "esp_log.h"

static int initialize_i2s(i2s_chan_handle_t *tx_channel, i2s_chan_handle_t *rx_channel) {
    i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    channel.dma_desc_num = 6;
    channel.dma_frame_num = 240;
    channel.auto_clear_after_cb = true;
    if (i2s_new_channel(&channel, tx_channel, rx_channel) != ESP_OK)
        return -1;
    const i2s_std_config_t bus = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(MYBOT_AUDIO_SAMPLE_RATE),
        .slot_cfg =
            I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg =
            {
                .mclk = MYBOT_AUDIO_I2S_MCLK,
                .bclk = MYBOT_AUDIO_I2S_BCLK,
                .ws = MYBOT_AUDIO_I2S_WS,
                .dout = MYBOT_AUDIO_I2S_DOUT,
                .din = MYBOT_AUDIO_I2S_DIN,
            },
    };
    return i2s_channel_init_std_mode(*tx_channel, &bus) == ESP_OK &&
                   i2s_channel_init_std_mode(*rx_channel, &bus) == ESP_OK
               ? 0
               : -1;
}
static int initialize_codec(const audio_codec_ctrl_if_t **ctrl_if,
                            const audio_codec_if_t **codec_if) {
    i2c_master_bus_handle_t bus = mosaico_hardware_i2c_bus();
    if (!bus)
        return -1;
    audio_codec_i2c_cfg_t control = {
        .port = I2C_NUM_0,
        .addr = 0x19 << 1,
        .bus_handle = bus,
    };
    *ctrl_if = audio_codec_new_i2c_ctrl(&control);
    if (!*ctrl_if)
        return -1;
    es8311_codec_cfg_t codec = {
        .ctrl_if = *ctrl_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH,
        /* Shared lifecycle owns PA, so capture alone cannot enable the speaker. */
        .pa_pin = GPIO_NUM_NC,
        .master_mode = false,
        .use_mclk = true,
        .digital_mic = false,
        .hw_gain = {.pa_voltage = 5.0f, .codec_dac_voltage = 3.3f},
        /* Cloud AEC uses SDK playback PCM; RX slot zero is the physical MIC. */
        .no_dac_ref = true,
        .mclk_div = 256,
    };
    *codec_if = es8311_codec_new(&codec);
    return *codec_if ? 0 : -1;
}
static int configure_codec(esp_codec_dev_handle_t device, int volume) {
    if (esp_codec_dev_set_in_gain(device, 24.0f) != ESP_CODEC_DEV_OK ||
        esp_codec_dev_set_out_vol(device, volume) != ESP_CODEC_DEV_OK)
        return -1;
    ESP_LOGI("mosaico_audio",
             "event=audio_bus action=start rate=16000 controller=i2s slots=2 "
             "bits=16 framing=philips mclk_multiple=256 tx_slots=mono_copy rx_slot=0");
    return 0;
}
const mybot_codec_audio_board_t mybot_codec_audio_board = {
    .tag = "mosaico_audio",
    .bus_slots = 2,
    .mclk_multiple = 256,
    .initialize_i2s = initialize_i2s,
    .initialize_codec = initialize_codec,
    .configure_codec = configure_codec,
};
const mybot_audio_capture_ops_t *mybot_mosaico_audio_capture_ops(void) {
    return mybot_codec_audio_capture_ops();
}
const mybot_audio_playback_ops_t *mybot_mosaico_audio_playback_ops(void) {
    return mybot_codec_audio_playback_ops();
}
const mybot_audio_volume_ops_t *mybot_mosaico_audio_volume_ops(void) {
    return mybot_codec_audio_volume_ops();
}
