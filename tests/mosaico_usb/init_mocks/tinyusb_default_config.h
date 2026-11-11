/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_TEST_MOSAICO_USB_INIT_MOCKS_TINYUSB_DEFAULT_CONFIG_H_
#define MYBOT_TEST_MOSAICO_USB_INIT_MOCKS_TINYUSB_DEFAULT_CONFIG_H_

#include "tinyusb.h"

#define TINYUSB_DEFAULT_CONFIG(callback, argument)                                                 \
    ((tinyusb_config_t){.phy = {.self_powered = true},                                             \
                        .task = {.xCoreID = -1},                                                   \
                        .event_cb = callback,                                                      \
                        .event_arg = argument})

#endif /* MYBOT_TEST_MOSAICO_USB_INIT_MOCKS_TINYUSB_DEFAULT_CONFIG_H_ */
