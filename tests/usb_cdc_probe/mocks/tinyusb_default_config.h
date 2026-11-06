/* SPDX-License-Identifier: Apache-2.0 */
#ifndef TEST_USB_PROBE_DEFAULT_CONFIG_H
#define TEST_USB_PROBE_DEFAULT_CONFIG_H
#include "tinyusb.h"
#define TINYUSB_DEFAULT_CONFIG(callback, arg)                                                      \
    ((tinyusb_config_t){.event_cb = (callback), .event_arg = (arg)})
#endif
