/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "tusb.h"
bool usbd_edpt_xfer(uint8_t port, uint8_t endpoint, uint8_t *buffer, uint16_t bytes);
