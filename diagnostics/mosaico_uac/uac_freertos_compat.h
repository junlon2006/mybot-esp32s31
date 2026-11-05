// SPDX-FileCopyrightText: 2026 MyBot Project Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#if CONFIG_FREERTOS_UNICORE
#error "This scoped UAC adapter requires the S31 dual-core FreeRTOS port"
#endif

/* The diagnostic drains USB data into its own playback queue. The package's
 * USB FIFO occupancy feedback therefore no longer measures the audio backlog. */
#define tud_audio_feedback_params_cb mosaico_uac_upstream_feedback_unused
#define tud_audio_rx_done_isr mosaico_uac_upstream_rx_done_isr

/* Force-included only for usb_device_uac 1.3.1. Its audio completion callbacks
 * run in TinyUSB's ISR but use task-only FreeRTOS spellings. Keep the immutable
 * package intact while choosing the correct notification API for each context. */
static inline BaseType_t mosaico_uac_notify(TaskHandle_t task) {
    if (xPortInIsrContext()) {
        BaseType_t woken = pdFALSE;
        vTaskNotifyGiveFromISR(task, &woken);
        if (woken) {
            portYIELD_FROM_ISR();
        }
        return pdPASS;
    }
    return xTaskNotifyGive(task);
}

#undef xTaskNotifyGive
#define xTaskNotifyGive(task) mosaico_uac_notify(task)

/* The S31 multi-core ISR variants use the same spinlock implementation and are
 * valid in both contexts. Task-only variants optionally assert when in an ISR. */
#undef portENTER_CRITICAL
#define portENTER_CRITICAL(mux) portENTER_CRITICAL_ISR(mux)
#undef portEXIT_CRITICAL
#define portEXIT_CRITICAL(mux) portEXIT_CRITICAL_ISR(mux)
