/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_TEST_MOSAICO_USB_AUDIO_MOCKS_DEVICE_DCD_H_
#define MYBOT_TEST_MOSAICO_USB_AUDIO_MOCKS_DEVICE_DCD_H_

#include "tusb.h"
#define DCD_EVENT_XFER_COMPLETE 7
#define DCD_EVENT_BUS_RESET 1
typedef struct {
    uint8_t rhport, event_id;
    struct {
        uint8_t ep_addr, result;
        uint32_t len;
    } xfer_complete;
} dcd_event_t;

#endif /* MYBOT_TEST_MOSAICO_USB_AUDIO_MOCKS_DEVICE_DCD_H_ */
