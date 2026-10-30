/* SPDX-License-Identifier: MIT */
#ifndef MYBOT_KORVO_S31_RGB_PANEL_H_
#define MYBOT_KORVO_S31_RGB_PANEL_H_

#include "esp_lcd_panel_ops.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    esp_lcd_panel_handle_t handle;
    void *framebuffers[2];
    size_t framebuffer_bytes;
} mybot_korvo_s31_rgb_panel_t;

int mybot_korvo_s31_rgb_panel_open(mybot_korvo_s31_rgb_panel_t *panel);
/* Stop DMA/callbacks while retaining the user-owned buffers referenced by LVGL. */
int mybot_korvo_s31_rgb_panel_stop(mybot_korvo_s31_rgb_panel_t *panel);
/* The caller must remove its LVGL display before releasing these buffers. */
int mybot_korvo_s31_rgb_panel_close(mybot_korvo_s31_rgb_panel_t *panel);

#ifdef __cplusplus
}
#endif

#endif /* MYBOT_KORVO_S31_RGB_PANEL_H_ */
