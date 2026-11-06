/* SPDX-License-Identifier: MIT */
#ifndef MYBOT_MOSAICO_PANEL_H_
#define MYBOT_MOSAICO_PANEL_H_

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One transfer fits the internal DMA buffer; descriptors never reference PSRAM. */
#define MYBOT_MOSAICO_DRAW_ROWS 40

typedef struct {
    esp_lcd_panel_handle_t handle;
    esp_lcd_panel_io_handle_t io;
    void *dma_buffer;
    size_t transfer_bytes;
    bool spi_started;
} mybot_mosaico_panel_t;

int mybot_mosaico_panel_open(mybot_mosaico_panel_t *panel);
/* The adapter must drain the outstanding color transfer before calling close.
 * Failed cleanup retains ownership for a later initialize/destroy attempt. */
int mybot_mosaico_panel_close(mybot_mosaico_panel_t *panel);

#ifdef __cplusplus
}
#endif
#endif /* MYBOT_MOSAICO_PANEL_H_ */
