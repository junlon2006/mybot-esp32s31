/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 *
 * CO5300 initialization and V1.2 wiring follow esp-mosaico-bsp
 * 8ce87a7ef130f946de4f84f6a5a20db1d8982605, onboard/display.c.
 */
#include "board_config.h"
#include "display/mosaico_panel.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_co5300.h"
#include "esp_lcd_io_spi.h"
#include "esp_log.h"

#include <stdint.h>

#define TAG "mosaico_panel"

_Static_assert(MYBOT_DISPLAY_WIDTH == 480 && MYBOT_DISPLAY_HEIGHT == 480,
               "Mosaico V1.2 uses the 480x480 CO5300 panel");

/* Official panel setup without TE: this adapter completes each partial update
 * before LVGL reuses the buffer. Panel orientation and touch both remain at 0. */
static const co5300_lcd_init_cmd_t vendor_init[] = {
    {0x11, NULL, 0, 600},
    {0xFE, (uint8_t[]){0x20}, 1, 0},
    {0x19, (uint8_t[]){0x10}, 1, 0},
    {0x1C, (uint8_t[]){0xA0}, 1, 0},
    {0xFE, (uint8_t[]){0x00}, 1, 0},
    {0xC4, (uint8_t[]){0x80}, 1, 0},
    {0x3A, (uint8_t[]){0x55}, 1, 0},
    {0x53, (uint8_t[]){0x20}, 1, 0},
    {0x51, (uint8_t[]){0xFF}, 1, 0},
    {0x63, (uint8_t[]){0xFF}, 1, 0},
    {0x2A, (uint8_t[]){0x00, 0x00, 0x01, 0xDF}, 4, 0},
    {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0xDF}, 4, 0},
    {0x29, NULL, 0, 600},
};

int mybot_mosaico_panel_close(mybot_mosaico_panel_t *panel) {
    if (!panel) {
        return -1;
    }
    if (panel->handle) {
        if (esp_lcd_panel_del(panel->handle) != ESP_OK) {
            return -1;
        }
        panel->handle = NULL;
    }
    if (panel->io) {
        /* Joins any finishing SPI callback before the adapter frees its state. */
        if (esp_lcd_panel_io_del(panel->io) != ESP_OK) {
            return -1;
        }
        panel->io = NULL;
    }
    if (panel->spi_started) {
        if (spi_bus_free(MYBOT_DISPLAY_SPI_HOST) != ESP_OK) {
            return -1;
        }
        panel->spi_started = false;
    }
    heap_caps_free(panel->dma_buffer);
    panel->dma_buffer = NULL;
    panel->transfer_bytes = 0;
    return 0;
}

int mybot_mosaico_panel_open(mybot_mosaico_panel_t *panel) {
    if (!panel || panel->handle || panel->io || panel->spi_started || panel->dma_buffer) {
        return -1;
    }
    panel->transfer_bytes =
        (size_t)MYBOT_DISPLAY_WIDTH * MYBOT_MOSAICO_DRAW_ROWS * sizeof(uint16_t);
    /* The small SPI staging buffer is internal DMA memory, with no external
     * cache maintenance or IDF bounce allocation needed in the render path. */
    panel->dma_buffer = heap_caps_calloc(1, panel->transfer_bytes,
                                         MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (!panel->dma_buffer) {
        goto failed;
    }
    const spi_bus_config_t bus_config = {
        .sclk_io_num = MYBOT_DISPLAY_SCLK,
        .data0_io_num = MYBOT_DISPLAY_DATA0,
        .data1_io_num = MYBOT_DISPLAY_DATA1,
        .data2_io_num = MYBOT_DISPLAY_DATA2,
        .data3_io_num = MYBOT_DISPLAY_DATA3,
        .data4_io_num = GPIO_NUM_NC,
        .data5_io_num = GPIO_NUM_NC,
        .data6_io_num = GPIO_NUM_NC,
        .data7_io_num = GPIO_NUM_NC,
        .max_transfer_sz = panel->transfer_bytes,
        .flags = SPICOMMON_BUSFLAG_QUAD,
    };
    if (spi_bus_initialize(MYBOT_DISPLAY_SPI_HOST, &bus_config, SPI_DMA_CH_AUTO) != ESP_OK) {
        goto failed;
    }
    panel->spi_started = true;
    const gpio_num_t pins[] = {MYBOT_DISPLAY_SCLK, MYBOT_DISPLAY_DATA0, MYBOT_DISPLAY_DATA1,
                               MYBOT_DISPLAY_DATA2, MYBOT_DISPLAY_DATA3};
    for (size_t i = 0; i < sizeof(pins) / sizeof(pins[0]); ++i) {
        if (gpio_set_drive_capability(pins[i], GPIO_DRIVE_CAP_2) != ESP_OK) {
            goto failed;
        }
    }
    esp_lcd_panel_io_spi_config_t io_config =
        CO5300_PANEL_IO_QSPI_CONFIG(MYBOT_DISPLAY_CS, NULL, NULL);
    io_config.trans_queue_depth = 1;
    io_config.flags.psram_dma_direct = false;
    if (esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)MYBOT_DISPLAY_SPI_HOST, &io_config,
                                 &panel->io) != ESP_OK) {
        goto failed;
    }
    const co5300_vendor_config_t vendor_config = {
        .init_cmds = vendor_init,
        .init_cmds_size = sizeof(vendor_init) / sizeof(vendor_init[0]),
        .flags.use_qspi_interface = true,
    };
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = MYBOT_DISPLAY_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = (void *)&vendor_config,
    };
    if (esp_lcd_new_panel_co5300(panel->io, &panel_config, &panel->handle) != ESP_OK ||
        esp_lcd_panel_reset(panel->handle) != ESP_OK ||
        esp_lcd_panel_init(panel->handle) != ESP_OK ||
        esp_lcd_panel_set_gap(panel->handle, 0, 0) != ESP_OK ||
        esp_lcd_panel_swap_xy(panel->handle, false) != ESP_OK ||
        esp_lcd_panel_mirror(panel->handle, false, false) != ESP_OK ||
        esp_lcd_panel_disp_on_off(panel->handle, true) != ESP_OK) {
        goto failed;
    }
    ESP_LOGI(TAG,
             "event=panel action=initialize result=ok width=480 height=480 qspi_hz=40000000 "
             "rotation=0 transfer_bytes=%zu memory=internal_dma",
             panel->transfer_bytes);
    return 0;
failed:
    if (mybot_mosaico_panel_close(panel) < 0) {
        ESP_LOGE(TAG, "event=panel action=cleanup result=pending");
    }
    return -1;
}
