/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_PRESENTER_H_
#define MYBOT_PRESENTER_H_

#include "mybot_lcd_internal.h"
#include "mybot_state_model.h"
#include <mybot/mybot_captions.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    mybot_lcd_t lcd;
    bool active;
    char source_language[33];
    char target_language[33];
    char error_code[64];
    int volume;
    bool media_ready;
    bool paused;
    mybot_translation_mode_t translation_mode;
    mybot_audio_source_t audio_source;
    bool usb_audio_available;
    mybot_translation_direction_t input_direction;
    uint32_t selection_revision;
    mybot_caption_state_t captions;
} mybot_presenter_t;

int mybot_presenter_init(mybot_presenter_t *presenter);
void mybot_presenter_deinit(mybot_presenter_t *presenter);
void mybot_presenter_show_screen(mybot_presenter_t *presenter, mybot_lcd_screen_t screen);
void mybot_presenter_show_pair_code(mybot_presenter_t *presenter, const char *code);
void mybot_presenter_set_translation(mybot_presenter_t *presenter, const char *source,
                                     const char *target, const char *error_code, int volume);
void mybot_presenter_render_state(mybot_presenter_t *presenter,
                                  const mybot_state_model_t *state_model);

#ifdef __cplusplus
}
#endif

#endif /* MYBOT_PRESENTER_H_ */
