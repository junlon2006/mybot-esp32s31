/* SPDX-License-Identifier: Apache-2.0 */
#include "usb/mosaico_usb_resampler.h"
#include <limits.h>
#include <string.h>

/* 95-tap Blackman-windowed sinc, 6.5 kHz cutoff at 48 kHz, unity DC Q15.
 * A real anti-alias filter is required before decimation; stereo mixing alone
 * does not reject frequencies above the 16 kHz destination's Nyquist limit. */
static const int16_t s_coefficients[MOSAICO_USB_FIR_TAPS] = {
    0,     0,    0,    0,    -1,   -2,   -1,   3,    7,    8,    1,     -12,   -21,  -16,
    6,     33,   44,   21,   -29,  -74,  -75,  -14,  80,   141,  107,   -24,   -177, -234,
    -122,  120,  336,  348,  89,   -314, -584, -470, 50,   688,  979,   579,   -438, -1503,
    -1787, -655, 1900, 5133, 7828, 8872, 7828, 5133, 1900, -655, -1787, -1503, -438, 579,
    979,   688,  50,   -470, -584, -314, 89,   348,  336,  120,  -122,  -234,  -177, -24,
    107,   141,  80,   -14,  -75,  -74,  -29,  21,   44,   33,   6,     -16,   -21,  -12,
    1,     8,    7,    3,    -1,   -2,   -1,   0,    0,    0,    0,
};
void mosaico_usb_resampler_reset(mosaico_usb_resampler_t *state) {
    memset(state, 0, sizeof(*state));
}
size_t mosaico_usb_resample(mosaico_usb_resampler_t *state, const int16_t *stereo,
                            size_t input_frames, int16_t *mono) {
    size_t produced = 0;
    for (size_t frame = 0; frame < input_frames; ++frame) {
        int32_t mixed = ((int32_t)stereo[frame * 2] + stereo[frame * 2 + 1]) / 2;
        state->history[state->head] = (int16_t)mixed;
        state->head = (state->head + 1) % MOSAICO_USB_FIR_TAPS;
        if (++state->phase != 3)
            continue;
        state->phase = 0;
        int64_t sum = 0;
        unsigned at = state->head;
        for (unsigned tap = 0; tap < MOSAICO_USB_FIR_TAPS; ++tap) {
            at = at ? at - 1 : MOSAICO_USB_FIR_TAPS - 1;
            sum += (int32_t)state->history[at] * s_coefficients[tap];
        }
        int32_t sample = (int32_t)(sum / 32768);
        if (sample > INT16_MAX)
            sample = INT16_MAX;
        if (sample < INT16_MIN)
            sample = INT16_MIN;
        mono[produced++] = (int16_t)sample;
    }
    return produced;
}

size_t mosaico_usb_interpolate(mosaico_usb_resampler_t *state, const int16_t *mono,
                               size_t input_frames, int16_t *output) {
    size_t produced = 0;
    for (size_t frame = 0; frame < input_frames; ++frame) {
        state->history[state->head] = mono[frame];
        state->head = (state->head + 1) % MOSAICO_USB_FIR_TAPS;
        for (unsigned phase = 0; phase < 3; ++phase) {
            int64_t sum = 0;
            unsigned at = state->head;
            for (unsigned tap = phase; tap < MOSAICO_USB_FIR_TAPS; tap += 3) {
                at = at ? at - 1 : MOSAICO_USB_FIR_TAPS - 1;
                sum += (int32_t)state->history[at] * s_coefficients[tap];
            }
            /* Normalize each quantized polyphase coefficient sum to unity.
             * This restores zero-insertion gain without a small DC ripple. */
            static const int32_t phase_gain[3] = {10924, 10924, 10920};
            int32_t sample = (int32_t)(sum / phase_gain[phase]);
            if (sample > INT16_MAX)
                sample = INT16_MAX;
            if (sample < INT16_MIN)
                sample = INT16_MIN;
            output[produced++] = (int16_t)sample;
        }
    }
    return produced;
}
