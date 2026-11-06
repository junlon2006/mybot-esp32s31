/* SPDX-License-Identifier: MIT */
#include "host_touch.h"
#include "display/mosaico_touch.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                   \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

struct host_touch_io {
    bool open;
} s_io;
static struct host_touch_handle s_handle;
static bool s_open;
static int s_irq_level = 1;
static bool s_pulse_on_level;
static unsigned s_reads, s_gets;
static uint8_t s_points;
static uint16_t s_x, s_y;
static esp_err_t s_read_result, s_get_result;
static bool s_fail_io, s_fail_new, s_fail_del, s_fail_io_del;
static int s_bus;

const char *esp_err_to_name(esp_err_t result) {
    return result == ESP_OK ? "OK" : "FAIL";
}
void touch_log(const char *format, ...) {
    (void)format;
}
int gpio_get_level(gpio_num_t pin) {
    CHECK(pin == 6);
    if (s_pulse_on_level) {
        s_pulse_on_level = false;
        CHECK(s_open && s_handle.config.interrupt_callback);
        s_handle.config.interrupt_callback(&s_handle);
    }
    return s_irq_level;
}
i2c_master_bus_handle_t mosaico_hardware_i2c_bus(void) {
    return &s_bus;
}
esp_err_t esp_lcd_new_panel_io_i2c(i2c_master_bus_handle_t bus,
                                   const esp_lcd_panel_io_i2c_config_t *config,
                                   esp_lcd_panel_io_handle_t *out) {
    CHECK(bus == &s_bus && !s_io.open);
    CHECK(config->scl_speed_hz == 400000 && config->transaction_timeout_ms == 100);
    if (s_fail_io) {
        s_fail_io = false;
        return ESP_FAIL;
    }
    s_io.open = true;
    *out = &s_io;
    return ESP_OK;
}
esp_err_t esp_lcd_panel_io_del(esp_lcd_panel_io_handle_t io) {
    CHECK(io == &s_io && s_io.open && !s_open);
    if (s_fail_io_del) {
        s_fail_io_del = false;
        return ESP_FAIL;
    }
    s_io.open = false;
    return ESP_OK;
}
esp_err_t esp_lcd_touch_new_i2c_cst9220(esp_lcd_panel_io_handle_t io,
                                        const esp_lcd_touch_config_t *config,
                                        esp_lcd_touch_handle_t *out) {
    CHECK(io == &s_io && s_io.open && !s_open);
    CHECK(config->x_max == 479 && config->y_max == 479);
    CHECK(config->int_gpio_num == 6 && config->rst_gpio_num == -1 && !config->levels.interrupt);
    CHECK(config->interrupt_callback && config->user_data);
    CHECK(!config->flags.swap_xy && !config->flags.mirror_x && config->flags.mirror_y);
    if (s_fail_new) {
        s_fail_new = false;
        *out = NULL;
        return ESP_FAIL;
    }
    s_handle.config = *config;
    s_open = true;
    *out = &s_handle;
    return ESP_OK;
}
esp_err_t esp_lcd_touch_del(esp_lcd_touch_handle_t handle) {
    CHECK(handle == &s_handle && s_open && s_io.open);
    if (s_fail_del) {
        s_fail_del = false;
        return ESP_FAIL;
    }
    memset(&s_handle, 0, sizeof(s_handle));
    s_open = false;
    return ESP_OK;
}
esp_err_t esp_lcd_touch_read_data(esp_lcd_touch_handle_t handle) {
    CHECK(handle == &s_handle && s_open);
    ++s_reads;
    return s_read_result;
}
esp_err_t esp_lcd_touch_get_data(esp_lcd_touch_handle_t handle, esp_lcd_touch_point_data_t *point,
                                 uint8_t *count, uint8_t max_count) {
    CHECK(handle == &s_handle && s_open && max_count == 1);
    ++s_gets;
    if (s_get_result != ESP_OK)
        return s_get_result;
    *count = s_points;
    *point = (esp_lcd_touch_point_data_t){s_x, (uint16_t)(s_handle.config.y_max - s_y)};
    return ESP_OK;
}
static void pulse(void) {
    CHECK(s_open && s_handle.config.interrupt_callback);
    s_handle.config.interrupt_callback(&s_handle);
}
static void sample(mybot_mosaico_touch_t *touch, bool valid, bool down, uint16_t x, uint16_t y) {
    uint16_t got_x = 1234, got_y = 1234;
    bool pressed = true;
    int result = mybot_mosaico_touch_read(touch, &got_x, &got_y, &pressed);
    CHECK((result == 0) == valid);
    CHECK(pressed == down);
    if (valid && down)
        CHECK(got_x == x && got_y == y);
}
int main(void) {
    mybot_mosaico_touch_t touch = {0};
    CHECK(mybot_mosaico_touch_open(&touch) == 0);
    CHECK(s_handle.config.user_data == &touch);
    sample(&touch, true, false, 0, 0);
    sample(&touch, true, false, 0, 0);
    CHECK(s_reads == 0 && s_gets == 0); /* Idle must not read an acknowledged AB/AB report. */

    s_points = 1;
    s_x = 240;
    s_y = 206;
    pulse(); /* A brief IRQ pulse was already deasserted before the 30 ms poll. */
    sample(&touch, true, true, 240, 273);
    CHECK(s_reads == 1 && s_gets == 1);
    sample(&touch, true, true, 240, 273);
    sample(&touch, true, true, 240, 273);
    CHECK(s_reads == 1); /* No new report must not manufacture a release while held. */

    s_irq_level = 0;
    s_x = 260;
    s_y = 199; /* Level fallback if no edge was latched. */
    sample(&touch, true, true, 260, 280);
    CHECK(s_reads == 2);
    s_irq_level = 1;
    s_points = 0;
    pulse();
    sample(&touch, true, false, 0, 0);
    sample(&touch, true, false, 0, 0);
    CHECK(s_reads == 3);

    /* A ready edge can arrive during the GPIO sample. Consume its latch with
     * this read, rather than rereading the already ACKed report next poll. */
    s_points = 1;
    s_irq_level = 0;
    s_pulse_on_level = true;
    sample(&touch, true, true, 260, 280);
    unsigned raced_reads = s_reads;
    s_irq_level = 1;
    sample(&touch, true, true, 260, 280);
    CHECK(s_reads == raced_reads && !s_pulse_on_level);
    s_points = 0;
    pulse();
    sample(&touch, true, false, 0, 0);

    s_points = 1;
    pulse();
    sample(&touch, true, true, 260, 280);
    s_read_result = ESP_FAIL;
    pulse();
    sample(&touch, false, false, 0, 0);
    unsigned failed_reads = s_reads;
    sample(&touch, false, false, 0, 0);
    sample(&touch, false, false, 0, 0);
    CHECK(s_reads == failed_reads); /* An error stays invalid until a new valid report. */
    s_read_result = ESP_OK;
    s_points = 0;
    pulse();
    sample(&touch, true, false, 0, 0); /* Actual release can rearm the input gate. */

    s_points = 1;
    s_x = 479;
    s_y = 0;
    pulse();
    sample(&touch, true, true, 479, 479);
    s_x = 0;
    s_y = 479;
    pulse();
    sample(&touch, true, true, 0, 0);
    s_x = 224;
    s_y = 198;
    pulse();
    sample(&touch, true, true, 224, 281); /* Captured Home tap. */
    s_x = 334;
    s_y = 73;
    pulse();
    sample(&touch, true, true, 334, 406); /* Captured volume gesture. */
    s_get_result = ESP_FAIL;
    pulse();
    sample(&touch, false, false, 0, 0);
    sample(&touch, false, false, 0, 0);
    s_get_result = ESP_OK;
    s_points = 0;
    pulse();
    sample(&touch, true, false, 0, 0);

    s_points = 1;
    s_x = 480;
    s_y = 20;
    pulse();
    sample(&touch, false, false, 0, 0);
    sample(&touch, false, false, 0, 0);
    s_x = 20;
    s_y = 480;
    pulse();
    sample(&touch, false, false, 0, 0);
    s_points = 0;
    pulse();
    sample(&touch, true, false, 0, 0);

    s_fail_del = true;
    CHECK(mybot_mosaico_touch_close(&touch) < 0 && touch.handle && touch.io);
    s_fail_io_del = true;
    CHECK(mybot_mosaico_touch_close(&touch) < 0 && !touch.handle && touch.io);
    CHECK(mybot_mosaico_touch_close(&touch) == 0 && !touch.handle && !touch.io);
    CHECK(mybot_mosaico_touch_close(&touch) == 0);
    CHECK(!s_open && !s_io.open);
    CHECK(mybot_mosaico_touch_open(&touch) == 0);
    sample(&touch, true, false, 0, 0); /* A fresh owner must not inherit pending/pressed/error. */
    CHECK(mybot_mosaico_touch_close(&touch) == 0);
    s_fail_io = true;
    CHECK(mybot_mosaico_touch_open(&touch) < 0 && !touch.io && !touch.handle);
    s_fail_new = true;
    CHECK(mybot_mosaico_touch_open(&touch) < 0 && !touch.io && !touch.handle);
    CHECK(!s_open && !s_io.open);
    puts("PASS mosaico_touch IRQ pulse/level, idle/hold/release, errors, bounds, cleanup");
    return 0;
}
