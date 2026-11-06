/* SPDX-License-Identifier: Apache-2.0 */
#ifndef TEST_USB_PROBE_TINYUSB_H
#define TEST_USB_PROBE_TINYUSB_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
typedef enum {
    TINYUSB_EVENT_ATTACHED,
    TINYUSB_EVENT_DETACHED,
    TINYUSB_EVENT_SUSPENDED,
    TINYUSB_EVENT_RESUMED,
} tinyusb_event_id_t;
typedef struct {
    tinyusb_event_id_t id;
} tinyusb_event_t;
typedef struct {
    struct {
        const char **string;
        size_t string_count;
    } descriptor;
    void (*event_cb)(tinyusb_event_t *, void *);
    void *event_arg;
} tinyusb_config_t;
#define TUSB_SPEED_HIGH 2
esp_err_t tinyusb_driver_install(const tinyusb_config_t *config);
esp_err_t tinyusb_driver_uninstall(void);
bool tud_cdc_n_connected(uint8_t interface);
void tud_cdc_n_read_flush(uint8_t interface);
void tud_cdc_n_write_clear(uint8_t interface);
int tud_speed_get(void);
bool tud_inited(void);
bool tud_connected(void);
bool tud_mounted(void);
#endif
