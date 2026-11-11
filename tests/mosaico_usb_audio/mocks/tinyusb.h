/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_TEST_MOSAICO_USB_AUDIO_MOCKS_TINYUSB_H_
#define MYBOT_TEST_MOSAICO_USB_AUDIO_MOCKS_TINYUSB_H_

#include <stdint.h>
#include "esp_err.h"
#define CONFIG_TINYUSB_SUSPEND_CALLBACK 1
#define CONFIG_TINYUSB_RESUME_CALLBACK 1
typedef enum {
    TINYUSB_EVENT_ATTACHED,
    TINYUSB_EVENT_DETACHED,
    TINYUSB_EVENT_SUSPENDED,
    TINYUSB_EVENT_RESUMED,
} tinyusb_event_id_t;
typedef struct {
    tinyusb_event_id_t id;
    uint8_t rhport;
} tinyusb_event_t;
typedef struct tinyusb_config tinyusb_config_t;

#endif /* MYBOT_TEST_MOSAICO_USB_AUDIO_MOCKS_TINYUSB_H_ */
