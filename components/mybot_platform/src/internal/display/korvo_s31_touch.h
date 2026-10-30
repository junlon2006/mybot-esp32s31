/* SPDX-License-Identifier: MIT */
#ifndef MYBOT_KORVO_S31_TOUCH_H_
#define MYBOT_KORVO_S31_TOUCH_H_

#include "esp_lcd_touch.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    esp_lcd_panel_io_handle_t io;
    esp_lcd_touch_handle_t handle;
    bool read_failed;
} mybot_korvo_s31_touch_t;

/* Serialized by the display lifecycle/LVGL lock; the board owns the I2C bus. */
int mybot_korvo_s31_touch_open(mybot_korvo_s31_touch_t *touch);
int mybot_korvo_s31_touch_read(mybot_korvo_s31_touch_t *touch, uint16_t *x, uint16_t *y,
                               bool *pressed);
int mybot_korvo_s31_touch_close(mybot_korvo_s31_touch_t *touch);

#ifdef __cplusplus
}
#endif

#endif /* MYBOT_KORVO_S31_TOUCH_H_ */
