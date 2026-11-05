/* SPDX-License-Identifier: MIT */
#include "board_config.h"
#include "display/mosaico_touch.h"
#include "mosaico_hardware.h"

#include "driver/gpio.h"
#include "esp_lcd_touch_cst9220.h"
#include "esp_log.h"

#define TAG "mosaico_touch"

static void report_ready(esp_lcd_touch_handle_t handle) {
    mybot_mosaico_touch_t *touch = handle->config.user_data;
    __atomic_store_n(&touch->report_pending, 1, __ATOMIC_RELEASE);
}

int mybot_mosaico_touch_close(mybot_mosaico_touch_t *touch) {
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
    touch->pressed = false;
    touch->x = touch->y = 0;
    __atomic_store_n(&touch->report_pending, 0, __ATOMIC_RELEASE);
    return 0;
}

int mybot_mosaico_touch_open(mybot_mosaico_touch_t *touch) {
    if (!touch || touch->io || touch->handle || !mosaico_hardware_i2c_bus()) {
        return -1;
    }
    esp_lcd_panel_io_i2c_config_t io_config = ESP_LCD_TOUCH_IO_I2C_CST9220_CONFIG();
    io_config.scl_speed_hz = 400000;
    io_config.transaction_timeout_ms = 100;
    const esp_lcd_touch_config_t config = {
        .x_max = MYBOT_DISPLAY_WIDTH - 1,
        .y_max = MYBOT_DISPLAY_HEIGHT - 1,
        .rst_gpio_num = GPIO_NUM_NC,
        .int_gpio_num = MYBOT_TOUCH_INT,
        .levels.interrupt = 0,
        /* On this V1.2 assembly, the controller's decoded Y increases upward:
         * physical bottom-slider taps report Y around 80. Match the CO5300's
         * top-left origin through the common touch driver's 479 - Y mapping. */
        .flags.mirror_y = true,
        .interrupt_callback = report_ready,
        .user_data = touch,
    };
    if (esp_lcd_new_panel_io_i2c(mosaico_hardware_i2c_bus(), &io_config, &touch->io) != ESP_OK ||
        esp_lcd_touch_new_i2c_cst9220(touch->io, &config, &touch->handle) != ESP_OK) {
        if (mybot_mosaico_touch_close(touch) < 0) {
            ESP_LOGE(TAG, "event=touch action=cleanup result=pending");
        }
        return -1;
    }
    ESP_LOGI(TAG, "event=touch action=initialize result=ok source=cst9220 rotation=0 int_gpio=%d",
             MYBOT_TOUCH_INT);
    return 0;
}

int mybot_mosaico_touch_read(mybot_mosaico_touch_t *touch, uint16_t *x, uint16_t *y,
                             bool *pressed) {
    if (!touch || !touch->handle || !x || !y || !pressed) {
        return -1;
    }
    *pressed = false;
    /* HYN212 replaces an acknowledged report with 0xAB. Polling that report
     * unconditionally produces INVALID_RESPONSE and cancels every gesture.
     * Latch short IRQ pulses; also sample the level for a report already ready
     * when the handler was installed. Between reports keep the last contact,
     * including while held, rather than synthesizing a release on every ACK. */
    bool ready = gpio_get_level(MYBOT_TOUCH_INT) == 0;
    bool pending = __atomic_exchange_n(&touch->report_pending, 0, __ATOMIC_ACQ_REL) != 0;
    if (!pending && !ready) {
        if (touch->read_failed)
            return -1;
        *x = touch->x;
        *y = touch->y;
        *pressed = touch->pressed;
        return 0;
    }
    esp_lcd_touch_point_data_t point = {0};
    uint8_t count = 0;
    esp_err_t result = esp_lcd_touch_read_data(touch->handle);
    if (result == ESP_OK) {
        result = esp_lcd_touch_get_data(touch->handle, &point, &count, 1);
    }
    if (result == ESP_OK && count &&
        (point.x >= MYBOT_DISPLAY_WIDTH || point.y >= MYBOT_DISPLAY_HEIGHT)) {
        ESP_LOGW(TAG, "event=touch action=read result=out_of_bounds x=%u y=%u", point.x, point.y);
        result = ESP_ERR_INVALID_RESPONSE;
    }
    if (result != ESP_OK) {
        if (!touch->read_failed) {
            ESP_LOGW(TAG, "event=touch action=read result=error error=%s", esp_err_to_name(result));
        }
        touch->read_failed = true;
        touch->pressed = false;
        return -1;
    }
    if (touch->read_failed) {
        ESP_LOGI(TAG, "event=touch action=read result=recovered");
        touch->read_failed = false;
    }
    bool previous = touch->pressed;
    touch->pressed = count > 0;
    if (touch->pressed) {
        touch->x = point.x;
        touch->y = point.y;
    }
    *x = touch->x;
    *y = touch->y;
    *pressed = touch->pressed;
    if (previous != touch->pressed)
        ESP_LOGI(TAG, "event=touch action=%s x=%u y=%u", touch->pressed ? "press" : "release",
                 touch->x, touch->y);
    return 0;
}
