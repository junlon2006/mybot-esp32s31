/* SPDX-License-Identifier: MIT */
#include "board_config.h"
#include "display/korvo_s31_rgb_panel.h"

#include "esp_cache.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_log.h"

#include <stdint.h>
#include <string.h>

#define TAG "korvo_s31_panel"
#define RGB_DMA_BURST_BYTES 128U

int mybot_korvo_s31_rgb_panel_stop(mybot_korvo_s31_rgb_panel_t *panel) {
    if (!panel) {
        return -1;
    }
    if (panel->handle) {
        /* The IDF destructor stops LCD/GDMA and joins its interrupt handlers.
         * User framebuffers remain owned by this object until close(). */
        if (esp_lcd_panel_del(panel->handle) != ESP_OK) {
            return -1;
        }
        panel->handle = NULL;
    }
    return 0;
}

int mybot_korvo_s31_rgb_panel_close(mybot_korvo_s31_rgb_panel_t *panel) {
    if (mybot_korvo_s31_rgb_panel_stop(panel) < 0) {
        return -1;
    }
    for (unsigned i = 0; i < 2; ++i) {
        heap_caps_free(panel->framebuffers[i]);
        panel->framebuffers[i] = NULL;
    }
    panel->framebuffer_bytes = 0;
    return 0;
}

int mybot_korvo_s31_rgb_panel_open(mybot_korvo_s31_rgb_panel_t *panel) {
    if (!panel || panel->handle || panel->framebuffers[0] || panel->framebuffers[1]) {
        return -1;
    }
    /* 128 bytes covers this S31 DMA burst and its cache line. Check the public
     * cache API as well; the RGB constructor validates DMA accessibility. */
    const size_t alignment = RGB_DMA_BURST_BYTES;
    panel->framebuffer_bytes =
        (size_t)MYBOT_DISPLAY_WIDTH * MYBOT_DISPLAY_HEIGHT * sizeof(uint16_t);
    for (unsigned i = 0; i < 2; ++i) {
        panel->framebuffers[i] =
            heap_caps_aligned_calloc(alignment, 1, panel->framebuffer_bytes,
                                     MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
        if (!panel->framebuffers[i] ||
            esp_cache_get_line_size_by_addr(panel->framebuffers[i]) > alignment) {
            goto failed;
        }
    }

    esp_lcd_rgb_panel_config_t config = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings =
            {
                .pclk_hz = MYBOT_DISPLAY_PCLK_HZ,
                .h_res = MYBOT_DISPLAY_WIDTH,
                .v_res = MYBOT_DISPLAY_HEIGHT,
                .hsync_pulse_width = MYBOT_DISPLAY_HSYNC_PULSE,
                .hsync_back_porch = MYBOT_DISPLAY_HSYNC_BACK_PORCH,
                .hsync_front_porch = MYBOT_DISPLAY_HSYNC_FRONT_PORCH,
                .vsync_pulse_width = MYBOT_DISPLAY_VSYNC_PULSE,
                .vsync_back_porch = MYBOT_DISPLAY_VSYNC_BACK_PORCH,
                .vsync_front_porch = MYBOT_DISPLAY_VSYNC_FRONT_PORCH,
                .flags = {.pclk_active_neg = true},
            },
        .data_width = 16,
        .in_color_format = LCD_COLOR_FMT_RGB565,
        .out_color_format = LCD_COLOR_FMT_RGB565,
        .num_fbs = 2,
        .user_fbs = {panel->framebuffers[0], panel->framebuffers[1]},
        .dma_burst_size = RGB_DMA_BURST_BYTES,
        .hsync_gpio_num = MYBOT_DISPLAY_HSYNC,
        .vsync_gpio_num = MYBOT_DISPLAY_VSYNC,
        .de_gpio_num = MYBOT_DISPLAY_DE,
        .pclk_gpio_num = MYBOT_DISPLAY_PCLK,
        .disp_gpio_num = GPIO_NUM_NC,
        .flags = {.fb_in_psram = true},
    };
    const gpio_num_t data_pins[16] = MYBOT_DISPLAY_RGB_DATA_PINS;
    for (unsigned i = 0; i < ESP_LCD_RGB_BUS_WIDTH_MAX; ++i) {
        config.data_gpio_nums[i] = i < 16 ? data_pins[i] : GPIO_NUM_NC;
    }
    /* SUB3 backlight and display-enable are NC in the pinned Korvo BSP.
     * Its routed SPI signals are not a second panel control interface. */
    if (esp_lcd_new_rgb_panel(&config, &panel->handle) != ESP_OK ||
        esp_lcd_panel_reset(panel->handle) != ESP_OK ||
        esp_lcd_panel_init(panel->handle) != ESP_OK) {
        goto failed;
    }
    ESP_LOGI(TAG,
             "event=panel action=initialize result=ok width=%d height=%d pclk_hz=%d "
             "framebuffers=2 framebuffer_bytes=%zu memory=psram",
             MYBOT_DISPLAY_WIDTH, MYBOT_DISPLAY_HEIGHT, MYBOT_DISPLAY_PCLK_HZ,
             panel->framebuffer_bytes);
    return 0;
failed:
    if (mybot_korvo_s31_rgb_panel_close(panel) < 0) {
        ESP_LOGE(TAG, "event=panel action=cleanup result=pending");
    }
    return -1;
}
