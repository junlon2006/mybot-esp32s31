/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_TEST_MOSAICO_USB_INIT_MOCKS_TINYUSB_H_
#define MYBOT_TEST_MOSAICO_USB_INIT_MOCKS_TINYUSB_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    unsigned id;
    uint8_t rhport;
} tinyusb_event_t;
typedef struct {
    struct {
        bool self_powered;
    } phy;
    struct {
        int xCoreID;
    } task;
    void (*event_cb)(tinyusb_event_t *, void *);
    void *event_arg;
    bool descriptors_configured;
} tinyusb_config_t;

esp_err_t tinyusb_driver_install(const tinyusb_config_t *config);
esp_err_t tinyusb_driver_uninstall(void);

#endif /* MYBOT_TEST_MOSAICO_USB_INIT_MOCKS_TINYUSB_H_ */
