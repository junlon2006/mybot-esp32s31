/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stddef.h>
#include <stdint.h>

#define MOSAICO_USB_FIR_TAPS 95U
typedef struct {
    int16_t history[MOSAICO_USB_FIR_TAPS];
    unsigned head;
    unsigned phase;
} mosaico_usb_resampler_t;

/* 48 kHz stereo S16 -> 16 kHz mono S16. Call on the capture worker only.
 * Output capacity must be at least (input_frames + 2) / 3 samples. */
size_t mosaico_usb_resample(mosaico_usb_resampler_t *state, const int16_t *stereo,
                            size_t input_frames, int16_t *mono);
void mosaico_usb_resampler_reset(mosaico_usb_resampler_t *state);

/* 16 kHz mono S16 -> 48 kHz mono S16, polyphase anti-imaging FIR.
 * Runs only on the playback worker. Output capacity is input_frames * 3.
 * The state is independent from the capture decimator. */
size_t mosaico_usb_interpolate(mosaico_usb_resampler_t *state, const int16_t *mono,
                               size_t input_frames, int16_t *output);
