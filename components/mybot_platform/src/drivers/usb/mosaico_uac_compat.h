/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdlib.h>
#include "esp_heap_caps.h"
#include "uac_freertos_compat.h"

/* UAC 1.3.1's singleton contains the buffers and state used in USB ISR context.
 * Production enables PSRAM, so its plain calloc must not use the default
 * large-allocation policy. This header is forced only into that component. */
static inline void *mosaico_uac_internal_calloc(size_t count, size_t size) {
    return heap_caps_calloc(count, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}
#define calloc(count, size) mosaico_uac_internal_calloc(count, size)

/* Observe alternate-interface lifetime without replacing immutable UAC source.
 * The lowercase close name also covers TinyUSB's legacy uppercase alias. */
#define tud_audio_set_itf_cb mosaico_uac_upstream_set_itf
#define tud_audio_set_itf_close_ep_cb mosaico_uac_upstream_close_itf
#define tud_audio_set_req_entity_cb mosaico_uac_upstream_set_req
#define tud_audio_get_req_entity_cb mosaico_uac_upstream_get_req
/* Production owns microphone packets directly at the USB transfer clock. */
#define tud_audio_tx_done_isr mosaico_uac_upstream_tx_done_isr
