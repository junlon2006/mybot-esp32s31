/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_TEST_MOSAICO_USB_INIT_MOCKS_TINYUSB_CDC_ACM_H_
#define MYBOT_TEST_MOSAICO_USB_INIT_MOCKS_TINYUSB_CDC_ACM_H_

#include "esp_err.h"

#define TINYUSB_CDC_ACM_0 0
typedef struct {
    int cdc_port;
} tinyusb_config_cdcacm_t;

esp_err_t tinyusb_cdcacm_init(const tinyusb_config_cdcacm_t *config);

#endif /* MYBOT_TEST_MOSAICO_USB_INIT_MOCKS_TINYUSB_CDC_ACM_H_ */
