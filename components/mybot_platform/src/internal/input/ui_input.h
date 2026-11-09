/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_UI_INPUT_H_
#define MYBOT_UI_INPUT_H_

#include <mybot/mybot_ui.h>
#include <mybot/platform/mybot_key.h>

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

int mybot_ui_input_start(int (*prepare)(void), bool (*pressed)(void));
int mybot_ui_input_attach(void **out_ctx, mybot_key_event_handler_t emit, void *user_data);
void mybot_ui_input_destroy(void *ctx);

/* LVGL owner only. A valid click requires a clean press/release in one SDK epoch. */
void mybot_ui_input_touch_sample(bool valid, bool pressed);
void mybot_ui_input_event(mybot_key_event_t event);

/* Validate the released touch gesture and submit one nonblocking UI command. */
int mybot_ui_input_command(const mybot_ui_command_t *command);

#ifdef __cplusplus
}
#endif

#endif /* MYBOT_UI_INPUT_H_ */
