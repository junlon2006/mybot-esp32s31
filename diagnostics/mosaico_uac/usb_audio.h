// SPDX-FileCopyrightText: 2026 MyBot Project Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "esp_err.h"

/* Start once after mosaico_audio_init(). Any failure requires a reboot. */
esp_err_t mosaico_usb_audio_start(void);
void mosaico_usb_audio_log_stats(void);
