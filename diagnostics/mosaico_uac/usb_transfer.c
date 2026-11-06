// SPDX-FileCopyrightText: 2026 MyBot Project Contributors
// SPDX-License-Identifier: Apache-2.0

#include "usb_transfer.h"

#include <stdatomic.h>

#include "audio.h"
#include "tusb.h"
#include "device/dcd.h"

/* Endpoint addresses from usb_device_uac 1.3.1's duplex descriptor. */
enum {
    AUDIO_OUT_ENDPOINT = 0x01,
    AUDIO_FEEDBACK_ENDPOINT = 0x81,
    AUDIO_MIC_ENDPOINT = 0x82,
};

_Static_assert(ATOMIC_INT_LOCK_FREE == 2, "USB ISR counters must be lock-free");
_Static_assert(MOSAICO_AUDIO_SAMPLE_RATE == 48000, "Nominal feedback expects fixed 48 kHz");

static atomic_uint s_fb_attempts;
static atomic_uint s_fb_submitted;
static atomic_uint s_fb_submit_errors;
static atomic_uint s_fb_completed;
static atomic_uint s_fb_errors;
static atomic_uint s_fb_bytes;
static atomic_uint s_fb_value;
static atomic_uint s_mic_completed;
static atomic_uint s_mic_errors;
static atomic_uint s_mic_bytes;
static atomic_uint s_out_errors;

extern bool __real_dcd_edpt_xfer(uint8_t rhport, uint8_t ep_addr, uint8_t *buffer,
                                 uint16_t total_bytes);
extern void __real_dcd_event_handler(const dcd_event_t *event, bool in_isr);

bool __wrap_dcd_edpt_xfer(uint8_t rhport, uint8_t ep_addr, uint8_t *buffer, uint16_t total_bytes) {
    if (ep_addr != AUDIO_FEEDBACK_ENDPOINT) {
        return __real_dcd_edpt_xfer(rhport, ep_addr, buffer, total_bytes);
    }

    /* TinyUSB 0.19 queues its first feedback before invoking the parameter
     * callback. Supply nominal feedback at submission, including that first
     * packet. This diagnostic has a fixed 48 kHz clock; drift control belongs
     * to the eventual production audio path. Preserve the full-speed path. */
    if (buffer != NULL && total_bytes == sizeof(uint32_t) && tud_speed_get() == TUSB_SPEED_HIGH) {
        const uint32_t nominal = ((uint32_t)MOSAICO_AUDIO_SAMPLE_RATE << 16) / 8000;
        tu_unaligned_write32(buffer, tu_htole32(nominal));
        atomic_store_explicit(&s_fb_value, nominal, memory_order_relaxed);
    }
    atomic_fetch_add_explicit(&s_fb_attempts, 1, memory_order_relaxed);
    bool submitted = __real_dcd_edpt_xfer(rhport, ep_addr, buffer, total_bytes);
    atomic_fetch_add_explicit(submitted ? &s_fb_submitted : &s_fb_submit_errors, 1,
                              memory_order_relaxed);
    return submitted;
}

void __wrap_dcd_event_handler(const dcd_event_t *event, bool in_isr) {
    /* Audio completions are handled directly in the USB ISR and may never
     * enter the TinyUSB event queue. Observe them before that dispatch. */
    if (event->event_id == DCD_EVENT_XFER_COMPLETE) {
        bool success = event->xfer_complete.result == XFER_RESULT_SUCCESS;
        switch (event->xfer_complete.ep_addr) {
        case AUDIO_FEEDBACK_ENDPOINT:
            atomic_fetch_add_explicit(&s_fb_completed, 1, memory_order_relaxed);
            atomic_fetch_add_explicit(success ? &s_fb_bytes : &s_fb_errors,
                                      success ? event->xfer_complete.len : 1, memory_order_relaxed);
            break;
        case AUDIO_MIC_ENDPOINT:
            atomic_fetch_add_explicit(&s_mic_completed, 1, memory_order_relaxed);
            atomic_fetch_add_explicit(success ? &s_mic_bytes : &s_mic_errors,
                                      success ? event->xfer_complete.len : 1, memory_order_relaxed);
            break;
        case AUDIO_OUT_ENDPOINT:
            if (!success) {
                atomic_fetch_add_explicit(&s_out_errors, 1, memory_order_relaxed);
            }
            break;
        default:
            break;
        }
    }
    __real_dcd_event_handler(event, in_isr);
}

mosaico_usb_transfer_stats_t mosaico_usb_transfer_stats(void) {
    return (mosaico_usb_transfer_stats_t){
        .fb_attempts = atomic_load_explicit(&s_fb_attempts, memory_order_relaxed),
        .fb_submitted = atomic_load_explicit(&s_fb_submitted, memory_order_relaxed),
        .fb_submit_errors = atomic_load_explicit(&s_fb_submit_errors, memory_order_relaxed),
        .fb_completed = atomic_load_explicit(&s_fb_completed, memory_order_relaxed),
        .fb_errors = atomic_load_explicit(&s_fb_errors, memory_order_relaxed),
        .fb_bytes = atomic_load_explicit(&s_fb_bytes, memory_order_relaxed),
        .fb_value = atomic_load_explicit(&s_fb_value, memory_order_relaxed),
        .mic_completed = atomic_load_explicit(&s_mic_completed, memory_order_relaxed),
        .mic_errors = atomic_load_explicit(&s_mic_errors, memory_order_relaxed),
        .mic_bytes = atomic_load_explicit(&s_mic_bytes, memory_order_relaxed),
        .out_errors = atomic_load_explicit(&s_out_errors, memory_order_relaxed),
    };
}
