/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include "tinyusb.h"

#define TINYUSB_DEFAULT_CONFIG(callback, argument)                                                 \
    ((tinyusb_config_t){.phy = {.self_powered = true},                                             \
                        .task = {.xCoreID = -1},                                                   \
                        .event_cb = callback,                                                      \
                        .event_arg = argument})
