/* SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD */
/* SPDX-FileCopyrightText: 2026 MyBot Project Contributors */
/* SPDX-License-Identifier: Apache-2.0 */
#include "mosaico_hardware.h"
#include "board_config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static i2c_master_bus_handle_t s_bus;
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
int mosaico_hardware_init(void) {
    if (s_bus)
        return 0;
    /* V1.2: release shutdown and turn on the active-low shared 3V3 rail. */
    if (configure_output(MYBOT_POWER_OFF_GPIO, GPIO_MODE_OUTPUT_OD, 1) < 0 ||
        configure_output(MYBOT_AUDIO_PA_GPIO, GPIO_MODE_OUTPUT, 0) < 0 ||
        configure_output(MYBOT_VCC_ENABLE_GPIO, GPIO_MODE_OUTPUT, 0) < 0)
        return -1;
    vTaskDelay(pdMS_TO_TICKS(10));
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
    ESP_LOGI("mosaico", "event=hardware board=v1.2 i2c=0 sda=56 scl=3 pa=45 result=ok");
    return 0;
}
i2c_master_bus_handle_t mosaico_hardware_i2c_bus(void) {
    return s_bus;
}
