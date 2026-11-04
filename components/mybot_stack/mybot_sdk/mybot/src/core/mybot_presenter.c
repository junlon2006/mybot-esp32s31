/* SPDX-License-Identifier: Apache-2.0 */
#include "mybot_presenter.h"

#include <api/aosl_log.h>
#include <stdio.h>
#include <string.h>

int mybot_presenter_init(mybot_presenter_t *presenter) {
    if (!presenter)
        return -1;
    presenter->volume = -1;
    if (!mybot_lcd_is_registered())
        return 0;
    if (mybot_lcd_init(&presenter->lcd) < 0)
        return -1;
    presenter->active = true;
    return 0;
}

void mybot_presenter_deinit(mybot_presenter_t *presenter) {
    if (presenter && presenter->active) {
        mybot_lcd_deinit(&presenter->lcd);
        presenter->active = false;
    }
}

void mybot_presenter_set_translation(mybot_presenter_t *presenter, const char *source,
                                     const char *target, const char *error_code, int volume) {
    if (!presenter)
        return;
    snprintf(presenter->source_language, sizeof(presenter->source_language), "%s",
             source ? source : "");
    snprintf(presenter->target_language, sizeof(presenter->target_language), "%s",
             target ? target : "");
    snprintf(presenter->error_code, sizeof(presenter->error_code), "%s",
             error_code ? error_code : "");
    presenter->volume = volume;
}

void mybot_presenter_show_screen(mybot_presenter_t *presenter, mybot_lcd_screen_t screen) {
    if (!presenter || !presenter->active)
        return;
    mybot_lcd_content_t content = {0};
    content.screen = screen;
    snprintf(content.source_language, sizeof(content.source_language), "%s",
             presenter->source_language);
    snprintf(content.target_language, sizeof(content.target_language), "%s",
             presenter->target_language);
    snprintf(content.error_code, sizeof(content.error_code), "%s", presenter->error_code);
    content.volume = presenter->volume;
    content.paused = presenter->paused && screen == MYBOT_LCD_SCREEN_IN_CONVERSATION;
    content.caption_generation = presenter->captions.generation;
    content.caption_revision = presenter->captions.revision;
    content.caption_count = presenter->captions.count;
    content.caption_first_sequence = presenter->captions.first_sequence;
    content.caption_latest_sequence = presenter->captions.latest_sequence;
    if (mybot_lcd_show_content(&presenter->lcd, &content) < 0) {
        AOSL_LOG_WRN("failed to render translation screen %d", (int)screen);
    }
}

void mybot_presenter_show_pair_code(mybot_presenter_t *presenter, const char *code) {
    if (presenter && presenter->active && mybot_lcd_show_pair_code(&presenter->lcd, code) < 0) {
        AOSL_LOG_WRN("failed to render pairing code");
    }
}

void mybot_presenter_render_state(mybot_presenter_t *presenter, const mybot_state_model_t *model) {
    if (!presenter || !model)
        return;
    mybot_state_view_t state = mybot_state_model_get_view(model);
    mybot_lcd_screen_t screen;
    switch (state.app_state) {
    case MYBOT_STATE_READY:
        screen = MYBOT_LCD_SCREEN_READY;
        break;
    case MYBOT_STATE_IN_CONVERSATION:
        screen = presenter->media_ready ? MYBOT_LCD_SCREEN_IN_CONVERSATION
                                        : MYBOT_LCD_SCREEN_TRANSLATION_STARTING;
        break;
    case MYBOT_STATE_TRANSLATION_STARTING:
        screen = MYBOT_LCD_SCREEN_TRANSLATION_STARTING;
        break;
    case MYBOT_STATE_TRANSLATION_STOPPING:
        screen = MYBOT_LCD_SCREEN_TRANSLATION_STOPPING;
        break;
    case MYBOT_STATE_CLEANUP_PENDING:
        screen = MYBOT_LCD_SCREEN_CLEANUP_PENDING;
        break;
    case MYBOT_STATE_START_UNKNOWN:
        screen = MYBOT_LCD_SCREEN_START_UNKNOWN;
        break;
    case MYBOT_STATE_TRANSLATION_ERROR:
        screen = MYBOT_LCD_SCREEN_TRANSLATION_ERROR;
        break;
    case MYBOT_STATE_WIFI_DISCONNECTED:
        screen = MYBOT_LCD_SCREEN_WIFI_DISCONNECTED;
        break;
    case MYBOT_STATE_PAIRING:
        if (state.device_state == MYBOT_DEVICE_STATE_AWAITING_CLAIM)
            return;
        screen = MYBOT_LCD_SCREEN_PAIRING;
        break;
    default:
        return;
    }
    mybot_presenter_show_screen(presenter, screen);
}
