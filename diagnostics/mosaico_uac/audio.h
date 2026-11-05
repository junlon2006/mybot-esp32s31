// SPDX-FileCopyrightText: 2026 MyBot Project Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stddef.h>

#include "esp_err.h"

#define MOSAICO_AUDIO_SAMPLE_RATE 48000
#define MOSAICO_AUDIO_CHANNELS 2
#define MOSAICO_AUDIO_BITS 16
#define MOSAICO_AUDIO_FRAME_BYTES 4

/* V1.2 only. Call once at boot; an initialization failure requires reboot. */
esp_err_t mosaico_audio_init(void);

/* Short local speaker test; call once before starting USB audio workers. */
esp_err_t mosaico_audio_test_tone(void);

/* Blocking PCM I/O for independent capture/playback workers, one caller per direction.
 * ESP_OK means the complete buffer transferred; errors can follow a partial transfer.
 * Buffers must be int16_t-aligned and contain whole interleaved stereo S16 frames. Capture
 * duplicates the mono microphone into both channels. Playback downmixes stereo in the writable
 * buffer. The codec's I2S adapter has a one-second wait timeout. */
esp_err_t mosaico_audio_read(void *data, size_t bytes);
esp_err_t mosaico_audio_write(void *data, size_t bytes);
