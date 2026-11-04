/* SPDX-License-Identifier: MIT */
#ifndef MYBOT_LVGL_VIEW_H_
#define MYBOT_LVGL_VIEW_H_

#include "lvgl.h"
#include <mybot/platform/mybot_lcd.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Shared LVGL view. The display owner serializes all calls with its LVGL lock.
 * The display and its active screen must outlive the view. The view owns its widgets
 * and releases them together on destroy. Update borrows
 * content only for the duration of the call; no FreeRTOS tasks are created here.
 * The simulator-derived layout requires the registered board's native 800 x 480
 * display. Its clock timer is owned and removed together with the view. */
int mybot_lvgl_view_create(lv_display_t *display);
int mybot_lvgl_view_create_sized(lv_display_t *display, int width, int height);
void mybot_lvgl_view_update(const mybot_lcd_content_t *content);
void mybot_lvgl_view_destroy(void);

#ifdef __cplusplus
}
#endif

#endif /* MYBOT_LVGL_VIEW_H_ */
