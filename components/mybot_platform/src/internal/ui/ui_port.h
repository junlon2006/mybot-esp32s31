/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_UI_PORT_H_
#define MYBOT_UI_PORT_H_

#include <mybot/mybot_captions.h>
#include <mybot/mybot_ui.h>
#include <mybot/platform/mybot_key.h>
#include <mybot/platform/mybot_lcd.h>

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MYBOT_UI_PORT_SSID_CAPACITY 33

const mybot_ui_language_t *mybot_ui_port_languages(size_t *count);
int mybot_ui_port_copy_caption(uint32_t generation, uint32_t sequence, mybot_caption_t *out);
bool mybot_ui_port_wifi_connected(void);
int mybot_ui_port_provisioning_ssid(char *ssid, size_t capacity);

/* All touch-originated UI commands share one gesture validation and submit path. */
int mybot_ui_port_command(const mybot_ui_command_t *command);
void mybot_ui_port_event(mybot_key_event_t event);

#ifdef __cplusplus
}
#endif

#endif /* MYBOT_UI_PORT_H_ */
