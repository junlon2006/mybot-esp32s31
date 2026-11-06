/* SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD */
/* SPDX-FileCopyrightText: 2026 MyBot Project Contributors */
/* SPDX-License-Identifier: Apache-2.0 */
/* Revision detection, retained GPIO levels and the VCC ramp derive from
 * esp-mosaico-bsp 8ce87a7ef130f946de4f84f6a5a20db1d8982605,
 * components/esp-mosaico-bsp/onboard/esp_mosaico.c. */
#include "mosaico_hardware.h"
#include "board_config.h"
#include "driver/ledc.h"
#include "esp_efuse.h"
#include "esp_efuse_table.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/gpio_reg.h"
#include "soc/soc.h"

#define TAG "mosaico"
#define VCC_OFF_LEVEL 1
#define VCC_ON_LEVEL 0
#define VCC_RAMP_MS 50

static i2c_master_bus_handle_t s_bus;
static bool s_power_ready;

static int check_revision(void) {
    uint16_t version = 0;
    esp_err_t result =
        esp_efuse_read_field_blob(ESP_EFUSE_USER_DATA, &version, sizeof(version) * 8U);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "event=hardware_revision result=error error=%s", esp_err_to_name(result));
        return -1;
    }
    if (version == 0x0100) {
        ESP_LOGE(TAG, "event=hardware_revision raw=0x%04x result=incompatible target=v1.2",
                 (unsigned)version);
        return -1;
    }
    if (version == 0x0101 || version == 0x0102) {
        ESP_LOGI(TAG, "event=hardware_revision raw=0x%04x result=%s pin_map=v1.2",
                 (unsigned)version, version == 0x0101 ? "compatible_v1.1" : "ok");
    } else {
        /* The production target is fixed by the user. Unlike the upstream BSP,
         * blank/unknown eFuse data may use this map, with an explicit warning. */
        ESP_LOGW(TAG,
                 "event=hardware_revision raw=0x%04x result=degraded policy=assume_v1.2_pin_map "
                 "reason=unknown_or_unprogrammed",
                 (unsigned)version);
    }
    return 0;
}

static int configure_output(gpio_num_t pin, gpio_mode_t mode, int level) {
    const gpio_config_t config = {
        .pin_bit_mask = UINT64_C(1) << pin,
        .mode = mode,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_set_level(pin, level) == ESP_OK && gpio_config(&config) == ESP_OK &&
                   gpio_hold_dis(pin) == ESP_OK
               ? 0
               : -1;
}

static int configured_output_level(gpio_num_t pin) {
    const uint32_t bit = BIT(pin < 32 ? pin : pin - 32);
    const uint32_t enable_reg = pin < 32 ? GPIO_ENABLE_REG : GPIO_ENABLE1_REG;
    const uint32_t output_reg = pin < 32 ? GPIO_OUT_REG : GPIO_OUT1_REG;
    return (REG_READ(enable_reg) & bit) ? (REG_READ(output_reg) & bit) != 0 : -1;
}

static int ramp_vcc_on(void) {
    const ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = LEDC_TIMER_1,
        .freq_hz = 100000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    const ledc_channel_config_t channel = {
        .gpio_num = MYBOT_VCC_ENABLE_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_1,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_1,
        .duty = (1U << LEDC_TIMER_8_BIT) - 1U,
        .hpoint = 0,
    };
    esp_err_t result = ledc_timer_config(&timer);
    bool channel_attempted = result == ESP_OK;
    if (result == ESP_OK)
        result = ledc_channel_config(&channel);
    esp_err_t installed = ESP_ERR_INVALID_STATE;
    if (result == ESP_OK) {
        installed = ledc_fade_func_install(0);
        result = installed == ESP_ERR_INVALID_STATE ? ESP_OK : installed;
    }
    if (result == ESP_OK)
        result = ledc_set_fade_with_time(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, 0, VCC_RAMP_MS);
    if (result == ESP_OK)
        result = ledc_fade_start(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, LEDC_FADE_WAIT_DONE);
    if (installed == ESP_OK)
        ledc_fade_func_uninstall();
    if (channel_attempted) {
        esp_err_t stopped = ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1,
                                      result == ESP_OK ? VCC_ON_LEVEL : VCC_OFF_LEVEL);
        if (result == ESP_OK)
            result = stopped;
    }
    /* Restore GPIO ownership for a stable rail and GPIO hold. Every failure
     * attempts to leave the active-low enable high; never bypass a failed ramp. */
    if (configure_output(MYBOT_VCC_ENABLE_GPIO, GPIO_MODE_OUTPUT,
                         result == ESP_OK ? VCC_ON_LEVEL : VCC_OFF_LEVEL) < 0) {
        (void)configure_output(MYBOT_VCC_ENABLE_GPIO, GPIO_MODE_OUTPUT, VCC_OFF_LEVEL);
        return -1;
    }
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "event=power_ramp result=error error=%s", esp_err_to_name(result));
        return -1;
    }
    ESP_LOGI(TAG, "event=power_ramp result=ok gpio=60 frequency_hz=100000 bits=8 duration_ms=%d",
             VCC_RAMP_MS);
    return 0;
}

int mosaico_hardware_init(void) {
    if (s_bus)
        return 0;
    /* V1.0 uses GPIO56 for codec power, not this profile's I2C SDA. Detect it
     * before touching any output, hold or bus, including the shared rail. */
    if (check_revision() < 0)
        return -1;
    if (!s_power_ready) {
        int previous = configured_output_level(MYBOT_VCC_ENABLE_GPIO);
        int initial = previous < 0 ? VCC_OFF_LEVEL : previous;
        if (configure_output(MYBOT_VCC_ENABLE_GPIO, GPIO_MODE_OUTPUT, initial) < 0 ||
            configure_output(MYBOT_POWER_OFF_GPIO, GPIO_MODE_OUTPUT_OD, 1) < 0 ||
            configure_output(MYBOT_AUDIO_PA_GPIO, GPIO_MODE_OUTPUT, 0) < 0)
            return -1;
        if (initial == VCC_OFF_LEVEL && ramp_vcc_on() < 0)
            return -1;
        if (initial == VCC_ON_LEVEL)
            ESP_LOGI(TAG, "event=power_ramp result=adopted_on gpio=60");
        /* The pinned audio startup keeps a 10 ms settle after rail enable. */
        vTaskDelay(pdMS_TO_TICKS(10));
        s_power_ready = true;
    }
    const i2c_master_bus_config_t config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = MYBOT_I2C_SDA_GPIO,
        .scl_io_num = MYBOT_I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    if (i2c_new_master_bus(&config, &s_bus) != ESP_OK)
        return -1;
    ESP_LOGI(TAG, "event=hardware board=v1.2 i2c=0 sda=56 scl=3 pa=45 result=ok");
    return 0;
}
i2c_master_bus_handle_t mosaico_hardware_i2c_bus(void) {
    return s_bus;
}
