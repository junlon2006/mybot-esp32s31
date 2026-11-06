/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_HOST_MOSAICO_HARDWARE_H_
#define MYBOT_HOST_MOSAICO_HARDWARE_H_
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE -2
const char *esp_err_to_name(esp_err_t result);
void hardware_log(const char *level, const char *format, ...) __attribute__((format(printf, 2, 3)));
#define ESP_LOGI(tag, ...) hardware_log("info", __VA_ARGS__)
#define ESP_LOGW(tag, ...) hardware_log("warning", __VA_ARGS__)
#define ESP_LOGE(tag, ...) hardware_log("error", __VA_ARGS__)

typedef int gpio_num_t;
typedef int gpio_mode_t;
#define GPIO_NUM_3 3
#define GPIO_NUM_45 45
#define GPIO_NUM_56 56
#define GPIO_NUM_57 57
#define GPIO_NUM_60 60
#define GPIO_MODE_OUTPUT 1
#define GPIO_MODE_OUTPUT_OD 2
#define GPIO_PULLUP_DISABLE 0
#define GPIO_PULLDOWN_DISABLE 0
#define GPIO_INTR_DISABLE 0
typedef struct {
    uint64_t pin_bit_mask;
    gpio_mode_t mode;
    int pull_up_en, pull_down_en, intr_type;
} gpio_config_t;
esp_err_t gpio_set_level(gpio_num_t pin, uint32_t level);
esp_err_t gpio_config(const gpio_config_t *config);
esp_err_t gpio_hold_dis(gpio_num_t pin);

#define GPIO_ENABLE_REG 1U
#define GPIO_ENABLE1_REG 2U
#define GPIO_OUT_REG 3U
#define GPIO_OUT1_REG 4U
#define BIT(number) (UINT32_C(1) << (number))
uint32_t hardware_register_read(uint32_t address);
#define REG_READ(address) hardware_register_read(address)

typedef uint32_t TickType_t;
#define pdMS_TO_TICKS(ms) (ms)
void vTaskDelay(TickType_t ticks);

typedef struct host_i2c_bus *i2c_master_bus_handle_t;
#define I2C_NUM_0 0
#define I2C_CLK_SRC_DEFAULT 0
typedef struct {
    int i2c_port, sda_io_num, scl_io_num, clk_source;
    uint32_t glitch_ignore_cnt;
    struct {
        bool enable_internal_pullup;
    } flags;
} i2c_master_bus_config_t;
esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t *config, i2c_master_bus_handle_t *out);

typedef struct {
    int field;
} esp_efuse_desc_t;
extern const esp_efuse_desc_t *ESP_EFUSE_USER_DATA[];
esp_err_t esp_efuse_read_field_blob(const esp_efuse_desc_t *field[], void *out, size_t bits);

typedef int ledc_mode_t;
typedef int ledc_channel_t;
typedef int ledc_timer_t;
typedef int ledc_fade_mode_t;
#define LEDC_LOW_SPEED_MODE 0
#define LEDC_TIMER_8_BIT 8
#define LEDC_TIMER_1 1
#define LEDC_CHANNEL_1 1
#define LEDC_AUTO_CLK 0
#define LEDC_INTR_DISABLE 0
#define LEDC_FADE_WAIT_DONE 1
typedef struct {
    ledc_mode_t speed_mode;
    int duty_resolution;
    ledc_timer_t timer_num;
    uint32_t freq_hz;
    int clk_cfg;
} ledc_timer_config_t;
typedef struct {
    int gpio_num;
    ledc_mode_t speed_mode;
    ledc_channel_t channel;
    int intr_type;
    ledc_timer_t timer_sel;
    uint32_t duty;
    int hpoint;
} ledc_channel_config_t;
esp_err_t ledc_timer_config(const ledc_timer_config_t *config);
esp_err_t ledc_channel_config(const ledc_channel_config_t *config);
esp_err_t ledc_fade_func_install(int flags);
void ledc_fade_func_uninstall(void);
esp_err_t ledc_set_fade_with_time(ledc_mode_t mode, ledc_channel_t channel, uint32_t duty, int ms);
esp_err_t ledc_fade_start(ledc_mode_t mode, ledc_channel_t channel, ledc_fade_mode_t wait);
esp_err_t ledc_stop(ledc_mode_t mode, ledc_channel_t channel, uint32_t idle_level);
#endif
