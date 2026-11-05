/* SPDX-License-Identifier: MIT */
#ifndef MYBOT_MOSAICO_TOUCH_H_
#define MYBOT_MOSAICO_TOUCH_H_

#include "esp_lcd_touch.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    esp_lcd_panel_io_handle_t io;
    esp_lcd_touch_handle_t handle;
    bool read_failed;
    bool pressed;
    uint16_t x;
    uint16_t y;
    /* ISR only latches readiness; I2C and cached coordinates belong to LVGL. */
    uint32_t report_pending;
} mybot_mosaico_touch_t;

/* Serialized by the display lifecycle/LVGL lock; the board owns the I2C bus. */
int mybot_mosaico_touch_open(mybot_mosaico_touch_t *touch);
int mybot_mosaico_touch_read(mybot_mosaico_touch_t *touch, uint16_t *x, uint16_t *y, bool *pressed);
int mybot_mosaico_touch_close(mybot_mosaico_touch_t *touch);

#ifdef __cplusplus
}
#endif

#endif /* MYBOT_MOSAICO_TOUCH_H_ */
