/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Call once from app_main. ESP_OK means the diagnostic task was created;
 * event=usb_host_ready confirms USB initialization. Runs until reboot and
 * cannot share the native USB controller with the CDC Device probe. */
esp_err_t mybot_usb_host_probe_start(void);

#ifdef __cplusplus
}
#endif
