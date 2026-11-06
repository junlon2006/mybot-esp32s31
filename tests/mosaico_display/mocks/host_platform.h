/* SPDX-License-Identifier: MIT */
#pragma once
#include "../../korvo_s31_display/mocks/host_platform.h"
#define ESP_ERR_INVALID_SIZE 0x104
#define LV_DISPLAY_RENDER_MODE_PARTIAL 0
#define LV_EVENT_INVALIDATE_AREA 1
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    int unused;
} esp_lcd_panel_io_event_data_t;
typedef bool (*io_callback_t)(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t *, void *);
typedef struct {
    io_callback_t on_color_trans_done;
} esp_lcd_panel_io_callbacks_t;
esp_err_t esp_lcd_panel_io_register_event_callbacks(esp_lcd_panel_io_handle_t,
                                                    const esp_lcd_panel_io_callbacks_t *, void *);
typedef struct {
    void *param;
} lv_event_t;
void *lv_event_get_param(lv_event_t *);
void lv_display_add_event_cb(lv_display_t *, void (*)(lv_event_t *), int, void *);
void lv_draw_sw_rgb565_swap(void *, uint32_t);
#ifdef __cplusplus
}
#endif
