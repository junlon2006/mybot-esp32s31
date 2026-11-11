/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_MOSAICO_USB_CONSOLE_H_
#define MYBOT_MOSAICO_USB_CONSOLE_H_

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Board-owned for the whole boot; CDC never waits for a connected host. */
esp_err_t mosaico_usb_console_init(void);
/* UAC and the composite controller are initialized, regardless of a connected
 * host, open audio stream or available CDC VFS console. Fixed until reboot. */
bool mosaico_usb_audio_initialized(void);

#ifdef __cplusplus
}
#endif

#endif /* MYBOT_MOSAICO_USB_CONSOLE_H_ */
