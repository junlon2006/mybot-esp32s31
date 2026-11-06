/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "tinyusb.h"
#include <mybot/platform/mybot_audio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* PC playback feeds an optional capture device. Only translated PCM written to
 * the optional playback device may reach USB microphone; otherwise it is silent.
 * Call once before the board installs TinyUSB. usb_device_uac 1.3.1 has no delete
 * API, so a failed attempt is retained until reboot and must not be retried. */
esp_err_t mosaico_usb_audio_prepare(void);

/* Optional meeting LISTEN source. SDK worker consumes 16 kHz mono S16 at its
 * existing 60 ms cadence. start requires an open host playback interface.
 * read never waits; bounded shortage returns zero PCM, stop invalidates old PCM.
 * Only one capture instance is permitted; normal codec capture is independent. */
const mybot_audio_capture_ops_t *mosaico_usb_audio_capture_ops(void);

/* Meeting SPEAK sink, 16 kHz mono S16 -> host 48 kHz stereo. start requires a
 * mounted USB device, not an open host microphone. Closed-interface writes are
 * discarded. stop invalidates queued PCM; at most one already submitted USB
 * packet (125 us HS / 1 ms FS) can remain in the controller. */
const mybot_audio_playback_ops_t *mosaico_usb_audio_playback_ops(void);

/* Assign to the board's single TinyUSB instance. No logging or blocking work. */
void mosaico_usb_audio_event(tinyusb_event_t *event, void *arg);

typedef struct {
    bool prepared;
    bool mounted;
    bool suspended;
    /* Configured USB link; this does not claim either audio interface is open. */
    bool ready;
    uint32_t mounts;
    uint32_t unmounts;
    uint32_t suspends;
    uint32_t resumes;
    uint32_t rx_packets;
    uint32_t rx_bytes;
    uint32_t rx_zero_packets;
    uint32_t out_errors;
    uint32_t output_callbacks;
    uint32_t discarded_bytes;
    uint32_t output_errors;
    uint32_t input_callbacks;
    uint32_t silence_bytes;
    uint32_t input_errors;
    uint32_t mic_completed;
    uint32_t mic_bytes;
    uint32_t mic_errors;
    uint32_t fb_attempts;
    uint32_t fb_submitted;
    uint32_t fb_submit_errors;
    uint32_t fb_completed;
    uint32_t fb_bytes;
    uint32_t fb_errors;
    uint32_t fb_value;
    bool muted;      /* Host speaker master control. */
    uint32_t volume; /* Master -50..0 dB mapped to 0..100, not linear PCM gain. */
    bool capture_started;
    bool speaker_open;
    bool microphone_open;
    uint32_t queued_frames;          /* 48 kHz stereo frames. */
    uint32_t capture_frames;         /* Resampled 16 kHz mono frames. */
    uint32_t capture_zero_frames;    /* Padded underflow/startup silence, 16 kHz. */
    uint32_t capture_dropped_frames; /* Oldest frames dropped, 48 kHz. */
    uint32_t capture_peak;           /* Peak of the last read after resampling. */
    bool playback_started;
    uint32_t playback_queued_frames; /* 48 kHz mono frames, duplicated only at USB. */
    uint32_t playback_frames;        /* Translated frames submitted to USB, 48 kHz. */
    uint32_t playback_zero_frames;   /* Clocked silence during underflow/priming. */
    uint32_t playback_dropped_frames;
    uint32_t playback_peak;
} mosaico_usb_audio_stats_t;

/* Cumulative independent atomic snapshots; counters wrap naturally at 32 bits. */
mosaico_usb_audio_stats_t mosaico_usb_audio_stats(void);
/* Call from a normal task, never from audio/USB/ISR callbacks. */
void mosaico_usb_audio_log_stats(void);

#ifdef __cplusplus
}
#endif
