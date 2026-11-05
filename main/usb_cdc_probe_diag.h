/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_USB_CDC_PROBE_DIAG_H_
#define MYBOT_USB_CDC_PROBE_DIAG_H_

/** Read and log the S31 native USB controller after USB driver initialization. */
void mybot_usb_cdc_probe_log_controller(void);

/** Dump allocated interrupts once per boot through the public ESP-IDF API. */
void mybot_usb_cdc_probe_log_interrupts(void);

#endif /* MYBOT_USB_CDC_PROBE_DIAG_H_ */
