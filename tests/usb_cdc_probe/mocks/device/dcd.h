/* SPDX-License-Identifier: Apache-2.0 */
#ifndef TEST_USB_PROBE_DCD_H
#define TEST_USB_PROBE_DCD_H
/* Match TinyUSB 0.19 event IDs exposed to the public device event hook. */
enum {
    DCD_EVENT_INVALID = 0,
    DCD_EVENT_BUS_RESET,
    DCD_EVENT_UNPLUGGED,
    DCD_EVENT_SOF,
    DCD_EVENT_SUSPEND,
    DCD_EVENT_RESUME,
    DCD_EVENT_SETUP_RECEIVED,
    DCD_EVENT_XFER_COMPLETE,
    DCD_EVENT_USBD_DEFER_FUNC,
};
#endif
