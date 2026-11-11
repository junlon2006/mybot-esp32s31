/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_TEST_MOSAICO_USB_DESCRIPTOR_MOCKS_TINYUSB_H_
#define MYBOT_TEST_MOSAICO_USB_DESCRIPTOR_MOCKS_TINYUSB_H_

/* Use the real pinned descriptor macros/types. Only ESP's driver install
 * configuration is replaced; this test starts no USB or FreeRTOS tasks. */
#include "common/tusb_common.h"
#include "class/audio/audio.h"
#include "class/cdc/cdc.h"
#include "device/usbd.h"

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103

typedef struct {
    const tusb_desc_device_t *device;
    const tusb_desc_device_qualifier_t *qualifier;
    const char **string;
    size_t string_count;
    const uint8_t *full_speed_config;
    const uint8_t *high_speed_config;
} tinyusb_desc_config_t;
typedef struct {
    unsigned caller_owned_marker;
    tinyusb_desc_config_t descriptor;
} tinyusb_config_t;

#endif /* MYBOT_TEST_MOSAICO_USB_DESCRIPTOR_MOCKS_TINYUSB_H_ */
