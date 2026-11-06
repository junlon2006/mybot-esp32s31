/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_MOSAICO_USB_DESCRIPTORS_H_
#define MYBOT_MOSAICO_USB_DESCRIPTORS_H_

#include "tinyusb.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    MYBOT_USB_ITF_AUDIO_CONTROL = 0,
    MYBOT_USB_ITF_SPEAKER = 1,
    MYBOT_USB_ITF_MICROPHONE = 2,
    MYBOT_USB_ITF_CDC = 3,
    MYBOT_USB_ITF_CDC_DATA = 4,
    MYBOT_USB_ITF_COUNT = 5,
    MYBOT_USB_EP_AUDIO_OUT = 0x01,
    MYBOT_USB_EP_AUDIO_FEEDBACK = 0x81,
    MYBOT_USB_EP_AUDIO_IN = 0x82,
    MYBOT_USB_EP_CDC_NOTIF = 0x83,
    MYBOT_USB_EP_CDC_OUT = 0x04,
    MYBOT_USB_EP_CDC_IN = 0x84,
};

/* Configure descriptors before the single esp_tinyusb driver is installed.
 * The serial uses the factory MAC-48; an unavailable/invalid identity fails
 * configuration instead of exposing a serial shared by all devices.
 * The returned pointers have firmware lifetime; the caller still owns PHY,
 * task configuration, event handling and class initialization. */
esp_err_t mybot_mosaico_usb_descriptors_configure(tinyusb_config_t *config);

#ifdef __cplusplus
}
#endif
#endif /* MYBOT_MOSAICO_USB_DESCRIPTORS_H_ */
