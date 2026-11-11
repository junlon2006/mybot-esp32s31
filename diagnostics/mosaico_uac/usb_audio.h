// SPDX-FileCopyrightText: 2026 MyBot Project Contributors
// SPDX-License-Identifier: Apache-2.0

#ifndef MYBOT_MOSAICO_UAC_USB_AUDIO_H_
#define MYBOT_MOSAICO_UAC_USB_AUDIO_H_

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Start once after mosaico_audio_init(). Any failure requires a reboot. */
esp_err_t mosaico_usb_audio_start(void);
void mosaico_usb_audio_log_stats(void);

#ifdef __cplusplus
}
#endif

#endif /* MYBOT_MOSAICO_UAC_USB_AUDIO_H_ */
