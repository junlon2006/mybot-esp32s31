/* SPDX-License-Identifier: Apache-2.0 */
#include "korvo_s31_hardware.h"
#include "driver/gpio.h"
#include "esp_log.h"
static i2c_master_bus_handle_t s_bus;
int korvo_s31_hardware_init(void) {
    if (s_bus)
        return 0;
    const gpio_config_t pa = {.pin_bit_mask = 1ULL << GPIO_NUM_7, .mode = GPIO_MODE_OUTPUT};
    if (gpio_config(&pa) != ESP_OK || gpio_set_level(GPIO_NUM_7, 0) != ESP_OK)
        return -1;
    const i2c_master_bus_config_t config = {.i2c_port = I2C_NUM_0,
                                            .sda_io_num = GPIO_NUM_0,
                                            .scl_io_num = GPIO_NUM_1,
                                            .clk_source = I2C_CLK_SRC_DEFAULT,
                                            .flags.enable_internal_pullup = true};
    if (i2c_new_master_bus(&config, &s_bus) != ESP_OK)
        return -1;
    ESP_LOGI("korvo_s31", "event=hardware i2c=0 sda=0 scl=1 pa=7 result=ok");
    return 0;
}
i2c_master_bus_handle_t korvo_s31_hardware_i2c_bus(void) {
    return s_bus;
}
