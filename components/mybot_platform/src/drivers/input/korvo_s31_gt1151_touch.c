/* SPDX-License-Identifier: MIT */
#include "board_config.h"
#include "display/korvo_s31_touch.h"
#include "korvo_s31_hardware.h"

#include "esp_lcd_touch_gt1151.h"
#include "esp_log.h"

#define TAG "korvo_s31_touch"

int mybot_korvo_s31_touch_close(mybot_korvo_s31_touch_t *touch) {
    if (!touch) {
        return -1;
    }
    if (touch->handle) {
        if (esp_lcd_touch_del(touch->handle) != ESP_OK) {
            return -1;
        }
        touch->handle = NULL;
    }
    if (touch->io) {
        if (esp_lcd_panel_io_del(touch->io) != ESP_OK) {
            return -1;
        }
        touch->io = NULL;
    }
    touch->read_failed = false;
    return 0;
}

int mybot_korvo_s31_touch_open(mybot_korvo_s31_touch_t *touch) {
    if (!touch || touch->io || touch->handle || !korvo_s31_hardware_i2c_bus()) {
        return -1;
    }
    esp_lcd_panel_io_i2c_config_t io_config = ESP_LCD_TOUCH_IO_I2C_GT1151_CONFIG();
    io_config.scl_speed_hz = 400000;
    io_config.transaction_timeout_ms = 100;
    const esp_lcd_touch_config_t config = {
        .x_max = MYBOT_DISPLAY_WIDTH,
        .y_max = MYBOT_DISPLAY_HEIGHT,
        .rst_gpio_num = GPIO_NUM_NC,
        .int_gpio_num = GPIO_NUM_NC,
    };
    if (esp_lcd_new_panel_io_i2c(korvo_s31_hardware_i2c_bus(), &io_config, &touch->io) != ESP_OK ||
        esp_lcd_touch_new_i2c_gt1151(touch->io, &config, &touch->handle) != ESP_OK) {
        if (mybot_korvo_s31_touch_close(touch) < 0) {
            ESP_LOGE(TAG, "event=touch action=cleanup result=pending");
        }
        return -1;
    }
    ESP_LOGI(TAG, "event=touch action=initialize result=ok source=gt1151 rotation=0 polling=true");
    return 0;
}

int mybot_korvo_s31_touch_read(mybot_korvo_s31_touch_t *touch, uint16_t *x, uint16_t *y,
                               bool *pressed) {
    if (!touch || !touch->handle || !x || !y || !pressed) {
        return -1;
    }
    *pressed = false;
    esp_lcd_touch_point_data_t point = {0};
    uint8_t count = 0;
    esp_err_t result = esp_lcd_touch_read_data(touch->handle);
    if (result == ESP_OK) {
        result = esp_lcd_touch_get_data(touch->handle, &point, &count, 1);
    }
    if (result != ESP_OK) {
        if (!touch->read_failed) {
            ESP_LOGW(TAG, "event=touch action=read result=error error=%s", esp_err_to_name(result));
        }
        touch->read_failed = true;
        return -1;
    }
    if (touch->read_failed) {
        ESP_LOGI(TAG, "event=touch action=read result=recovered");
        touch->read_failed = false;
    }
    if (count && point.x < MYBOT_DISPLAY_WIDTH && point.y < MYBOT_DISPLAY_HEIGHT) {
        *x = point.x;
        *y = point.y;
        *pressed = true;
    }
    return 0;
}
