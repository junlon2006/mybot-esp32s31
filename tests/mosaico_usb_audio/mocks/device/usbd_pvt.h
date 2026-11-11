/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_TEST_MOSAICO_USB_AUDIO_MOCKS_DEVICE_USBD_PVT_H_
#define MYBOT_TEST_MOSAICO_USB_AUDIO_MOCKS_DEVICE_USBD_PVT_H_

#include "tusb.h"
bool usbd_edpt_xfer(uint8_t port, uint8_t endpoint, uint8_t *buffer, uint16_t bytes);

#endif /* MYBOT_TEST_MOSAICO_USB_AUDIO_MOCKS_DEVICE_USBD_PVT_H_ */
