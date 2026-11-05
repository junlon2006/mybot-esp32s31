/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_USB_CDC_PROBE_H_
#define MYBOT_USB_CDC_PROBE_H_

#include "esp_err.h"

/** Install the optional native CDC echo diagnostic once for this boot. */
esp_err_t mybot_usb_cdc_probe_start(void);

#endif /* MYBOT_USB_CDC_PROBE_H_ */
