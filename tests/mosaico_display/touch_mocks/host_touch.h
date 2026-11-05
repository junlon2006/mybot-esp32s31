/* SPDX-License-Identifier: MIT */
#ifndef MYBOT_MOSAICO_HOST_TOUCH_H_
#define MYBOT_MOSAICO_HOST_TOUCH_H_
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef int esp_err_t;
typedef int gpio_num_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_RESPONSE 0x108
#define GPIO_NUM_NC -1
#define IRAM_ATTR
const char *esp_err_to_name(esp_err_t result);
void touch_log(const char *format, ...) __attribute__((format(printf, 1, 2)));
#define ESP_LOGI(tag, ...) touch_log(__VA_ARGS__)
#define ESP_LOGW(tag, ...) touch_log(__VA_ARGS__)
#define ESP_LOGE(tag, ...) touch_log(__VA_ARGS__)
typedef void *i2c_master_bus_handle_t;
typedef struct host_touch_io *esp_lcd_panel_io_handle_t;
typedef struct host_touch_handle *esp_lcd_touch_handle_t;
typedef struct {
    uint16_t x_max, y_max;
    gpio_num_t rst_gpio_num, int_gpio_num;
    struct {
        unsigned reset : 1, interrupt : 1;
    } levels;
    struct {
        unsigned swap_xy : 1, mirror_x : 1, mirror_y : 1;
    } flags;
    void (*interrupt_callback)(esp_lcd_touch_handle_t);
    void *user_data;
} esp_lcd_touch_config_t;
struct host_touch_handle {
    esp_lcd_touch_config_t config;
};
typedef struct {
    uint16_t x, y;
} esp_lcd_touch_point_data_t;
typedef struct {
    unsigned scl_speed_hz;
    int transaction_timeout_ms;
} esp_lcd_panel_io_i2c_config_t;
#define ESP_LCD_TOUCH_IO_I2C_CST9220_CONFIG() {.scl_speed_hz = 400000}
int gpio_get_level(gpio_num_t pin);
i2c_master_bus_handle_t mosaico_hardware_i2c_bus(void);
esp_err_t esp_lcd_new_panel_io_i2c(i2c_master_bus_handle_t, const esp_lcd_panel_io_i2c_config_t *,
                                   esp_lcd_panel_io_handle_t *);
esp_err_t esp_lcd_panel_io_del(esp_lcd_panel_io_handle_t);
esp_err_t esp_lcd_touch_new_i2c_cst9220(esp_lcd_panel_io_handle_t, const esp_lcd_touch_config_t *,
                                        esp_lcd_touch_handle_t *);
esp_err_t esp_lcd_touch_del(esp_lcd_touch_handle_t);
esp_err_t esp_lcd_touch_read_data(esp_lcd_touch_handle_t);
esp_err_t esp_lcd_touch_get_data(esp_lcd_touch_handle_t, esp_lcd_touch_point_data_t *, uint8_t *,
                                 uint8_t);
#endif
