// SPDX-FileCopyrightText: 2026 MyBot Project Contributors
// SPDX-License-Identifier: Apache-2.0

#ifndef MYBOT_MOSAICO_UAC_USB_TRANSFER_H_
#define MYBOT_MOSAICO_UAC_USB_TRANSFER_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t fb_attempts;
    uint32_t fb_submitted;
    uint32_t fb_submit_errors;
    uint32_t fb_completed;
    uint32_t fb_errors;
    uint32_t fb_bytes;
    uint32_t fb_value;
    uint32_t mic_completed;
    uint32_t mic_errors;
    uint32_t mic_bytes;
    uint32_t out_errors;
} mosaico_usb_transfer_stats_t;

/* Cumulative counters since boot; fields are independent atomic snapshots. */
mosaico_usb_transfer_stats_t mosaico_usb_transfer_stats(void);

#ifdef __cplusplus
}
#endif

#endif /* MYBOT_MOSAICO_UAC_USB_TRANSFER_H_ */
