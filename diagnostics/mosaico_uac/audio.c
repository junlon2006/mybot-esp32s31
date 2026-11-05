// SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
// SPDX-FileCopyrightText: 2026 MyBot Project Contributors
// SPDX-License-Identifier: Apache-2.0

#include "audio.h"

#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* ESP-Mosaico V1.2 pin map, from the official BSP and CoreBoard V1.2 schematic. */
#define VCC_ENABLE_GPIO GPIO_NUM_60
#define POWER_OFF_GPIO GPIO_NUM_57
#define CODEC_SDA_GPIO GPIO_NUM_56
#define CODEC_SCL_GPIO GPIO_NUM_3
#define CODEC_MCLK_GPIO GPIO_NUM_54
#define CODEC_BCLK_GPIO GPIO_NUM_37
#define CODEC_WS_GPIO GPIO_NUM_49
#define CODEC_DOUT_GPIO GPIO_NUM_52
#define CODEC_DIN_GPIO GPIO_NUM_40
#define CODEC_PA_GPIO GPIO_NUM_45
#define CODEC_ADDRESS 0x19

static const char *TAG = "mosaico_audio";
static esp_codec_dev_handle_t s_codec;
static bool s_init_attempted;
static bool s_ready;

static esp_err_t configure_output(gpio_num_t pin, gpio_mode_t mode, int level) {
    const gpio_config_t config = {
        .pin_bit_mask = UINT64_C(1) << pin,
        .mode = mode,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_set_level(pin, level), TAG, "preset GPIO%d", pin);
    ESP_RETURN_ON_ERROR(gpio_config(&config), TAG, "configure GPIO%d", pin);
    return gpio_hold_dis(pin);
}

esp_err_t mosaico_audio_init(void) {
    ESP_RETURN_ON_FALSE(!s_init_attempted, ESP_ERR_INVALID_STATE, TAG,
                        "already initialized/attempted");
    s_init_attempted = true;

    /* Release the active-low shutdown signal; V1.2 has no separate codec GPIO rail. */
    ESP_RETURN_ON_ERROR(configure_output(POWER_OFF_GPIO, GPIO_MODE_OUTPUT_OD, 1), TAG,
                        "release power-off signal");
    ESP_RETURN_ON_ERROR(configure_output(CODEC_PA_GPIO, GPIO_MODE_OUTPUT, 0), TAG, "disable PA");
    ESP_RETURN_ON_ERROR(configure_output(VCC_ENABLE_GPIO, GPIO_MODE_OUTPUT, 0), TAG,
                        "enable VCC_3V3");
    vTaskDelay(pdMS_TO_TICKS(10));

    i2c_master_bus_handle_t i2c_bus = NULL;
    const i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = CODEC_SDA_GPIO,
        .scl_io_num = CODEC_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_config, &i2c_bus), TAG,
                        "create V1.2 audio I2C bus");
    ESP_RETURN_ON_ERROR(i2c_master_probe(i2c_bus, CODEC_ADDRESS, 100), TAG,
                        "ES8311 not detected at 0x19");

    i2s_chan_handle_t tx_channel = NULL;
    i2s_chan_handle_t rx_channel = NULL;
    i2s_chan_config_t channel_config = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    channel_config.auto_clear = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&channel_config, &tx_channel, &rx_channel), TAG,
                        "create full-duplex I2S");
    const i2s_std_config_t i2s_config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(MOSAICO_AUDIO_SAMPLE_RATE),
        .slot_cfg =
            I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg =
            {
                .mclk = CODEC_MCLK_GPIO,
                .bclk = CODEC_BCLK_GPIO,
                .ws = CODEC_WS_GPIO,
                .dout = CODEC_DOUT_GPIO,
                .din = CODEC_DIN_GPIO,
            },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(tx_channel, &i2s_config), TAG,
                        "initialize I2S TX");
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(rx_channel, &i2s_config), TAG,
                        "initialize I2S RX");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(tx_channel), TAG, "enable I2S TX");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(rx_channel), TAG, "enable I2S RX");

    audio_codec_i2s_cfg_t data_config = {
        .port = I2S_NUM_0,
        .tx_handle = tx_channel,
        .rx_handle = rx_channel,
    };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&data_config);
    ESP_RETURN_ON_FALSE(data_if, ESP_ERR_NO_MEM, TAG, "create I2S data interface");
    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    ESP_RETURN_ON_FALSE(gpio_if, ESP_ERR_NO_MEM, TAG, "create codec GPIO interface");
    audio_codec_i2c_cfg_t control_config = {
        .addr = CODEC_ADDRESS << 1,
        .bus_handle = i2c_bus,
    };
    const audio_codec_ctrl_if_t *control_if = audio_codec_new_i2c_ctrl(&control_config);
    ESP_RETURN_ON_FALSE(control_if, ESP_ERR_NO_MEM, TAG, "create codec I2C interface");
    es8311_codec_cfg_t codec_config = {
        .ctrl_if = control_if,
        .gpio_if = gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH,
        .pa_pin = CODEC_PA_GPIO,
        .master_mode = false,
        .use_mclk = true,
        .digital_mic = false,
        .hw_gain =
            {
                .pa_voltage = 5.0f,
                .codec_dac_voltage = 3.3f,
            },
        .no_dac_ref = true,
        .mclk_div = 256,
    };
    const audio_codec_if_t *codec_if = es8311_codec_new(&codec_config);
    ESP_RETURN_ON_FALSE(codec_if, ESP_FAIL, TAG, "initialize ES8311");
    esp_codec_dev_cfg_t device_config = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN_OUT,
        .codec_if = codec_if,
        .data_if = data_if,
    };
    s_codec = esp_codec_dev_new(&device_config);
    ESP_RETURN_ON_FALSE(s_codec, ESP_ERR_NO_MEM, TAG, "create full-duplex codec device");
    esp_codec_dev_sample_info_t sample_info = {
        .bits_per_sample = MOSAICO_AUDIO_BITS,
        .channel = MOSAICO_AUDIO_CHANNELS,
        .sample_rate = MOSAICO_AUDIO_SAMPLE_RATE,
        .mclk_multiple = 256,
    };
    ESP_RETURN_ON_ERROR(esp_codec_dev_open(s_codec, &sample_info), TAG, "open full-duplex codec");
    /* Maximum codec volume; USB still applies the host's volume and mute. */
    ESP_RETURN_ON_ERROR(esp_codec_dev_set_out_vol(s_codec, 100), TAG, "set speaker volume");
    ESP_RETURN_ON_ERROR(esp_codec_dev_set_in_gain(s_codec, 24.0f), TAG, "set microphone gain");
    s_ready = true;
    ESP_LOGI(TAG, "event=audio_ready board=mosaico_v1.2 rate=48000 bits=16 channels=2 "
                  "volume=100 mic_gain_db=24 mic=mono_duplicated speaker=stereo_downmix");
    return ESP_OK;
}

static bool valid_buffer(const void *data, size_t bytes) {
    return data && (uintptr_t)data % _Alignof(int16_t) == 0 && bytes > 0 && bytes <= INT_MAX &&
           bytes % MOSAICO_AUDIO_FRAME_BYTES == 0;
}

esp_err_t mosaico_audio_read(void *data, size_t bytes) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!valid_buffer(data, bytes)) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t result = esp_codec_dev_read(s_codec, data, (int)bytes);
    if (result == ESP_OK) {
        int16_t *samples = data;
        for (size_t i = 0; i < bytes / sizeof(*samples); i += MOSAICO_AUDIO_CHANNELS) {
            samples[i + 1] = samples[i];
        }
    }
    return result;
}

esp_err_t mosaico_audio_write(void *data, size_t bytes) {
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!valid_buffer(data, bytes)) {
        return ESP_ERR_INVALID_ARG;
    }
    int16_t *samples = data;
    for (size_t i = 0; i < bytes / sizeof(*samples); i += MOSAICO_AUDIO_CHANNELS) {
        int16_t mono = (int16_t)(((int32_t)samples[i] + samples[i + 1]) / 2);
        samples[i] = mono;
        samples[i + 1] = mono;
    }
    return esp_codec_dev_write(s_codec, data, (int)bytes);
}

esp_err_t mosaico_audio_test_tone(void) {
    static int16_t samples[MOSAICO_AUDIO_SAMPLE_RATE / 100 * MOSAICO_AUDIO_CHANNELS];
    /* Two 150 ms local tones, separated by 120 ms silence, before USB starts.
     * A heard tone checks codec/I2S/PA output independently of host routing. */
    for (unsigned block = 0; block < 50; ++block) {
        bool tone = block < 15 || (block >= 27 && block < 42);
        for (unsigned frame = 0; frame < MOSAICO_AUDIO_SAMPLE_RATE / 100; ++frame) {
            int16_t value = tone ? (int16_t)(8192.0f * sinf(6.283185307f * frame / 48.0f)) : 0;
            samples[frame * 2] = value;
            samples[frame * 2 + 1] = value;
        }
        ESP_RETURN_ON_ERROR(mosaico_audio_write(samples, sizeof(samples)), TAG,
                            "write local speaker test tone");
    }
    ESP_LOGI(TAG, "event=speaker_tone queued=yes frequency_hz=1000 count=2");
    return ESP_OK;
}
