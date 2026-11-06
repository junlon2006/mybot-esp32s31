/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include "esp_err.h"

/* Board-owned for the whole boot; CDC never waits for a connected host. */
esp_err_t mosaico_usb_console_init(void);
