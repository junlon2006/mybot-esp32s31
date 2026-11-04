/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_KORVO_S31_KEYS_H_
#define MYBOT_KORVO_S31_KEYS_H_

#include <stdbool.h>
#include <mybot/platform/mybot_key.h>
#include <mybot/mybot_ui.h>

#ifdef __cplusplus
extern "C" {
#endif

/* LVGL owner only. Samples validate a clean press/release; they never trigger
 * an action. A read error or detached SDK cancels the gesture. */
void mybot_korvo_s31_keys_touch_sample(bool valid, bool pressed);
/* Called by an LVGL button CLICKED callback after a validated release. A bounded
 * mailbox forwards the event to the board key worker without taking SDK locks. */
void mybot_korvo_s31_keys_ui_event(mybot_key_event_t event);
/* Copies one parameterized command after the same validated release. */
void mybot_korvo_s31_keys_ui_command(const mybot_ui_command_t *command);

#ifdef __cplusplus
}
#endif

#endif /* MYBOT_KORVO_S31_KEYS_H_ */
