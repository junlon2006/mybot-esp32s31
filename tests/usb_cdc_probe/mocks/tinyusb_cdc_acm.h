/* SPDX-License-Identifier: Apache-2.0 */
#ifndef TEST_USB_PROBE_CDC_ACM_H
#define TEST_USB_PROBE_CDC_ACM_H
#include "tinyusb.h"
#define TINYUSB_CDC_ACM_0 0
typedef enum {
    CDC_EVENT_RX,
    CDC_EVENT_LINE_STATE_CHANGED,
} cdcacm_event_type_t;
typedef struct {
    cdcacm_event_type_t type;
    struct {
        bool dtr;
        bool rts;
    } line_state_changed_data;
} cdcacm_event_t;
typedef struct {
    int cdc_port;
    void (*callback_rx)(int, cdcacm_event_t *);
    void (*callback_line_state_changed)(int, cdcacm_event_t *);
} tinyusb_config_cdcacm_t;
esp_err_t tinyusb_cdcacm_init(const tinyusb_config_cdcacm_t *config);
esp_err_t tinyusb_cdcacm_deinit(int interface);
esp_err_t tinyusb_cdcacm_read(int interface, uint8_t *buffer, size_t capacity, size_t *received);
size_t tinyusb_cdcacm_write_queue(int interface, const uint8_t *buffer, size_t length);
esp_err_t tinyusb_cdcacm_write_flush(int interface, uint32_t timeout);
#endif
