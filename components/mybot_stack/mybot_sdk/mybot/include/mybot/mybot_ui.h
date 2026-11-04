/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_UI_H_
#define MYBOT_UI_H_

#include <mybot/mybot_export.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *code;
    const char *name_zh;
    const char *name_en;
} mybot_ui_language_t;

typedef enum {
    MYBOT_UI_SET_LANGUAGES,
    MYBOT_UI_SET_VOLUME,
    MYBOT_UI_SET_PAUSED,
} mybot_ui_command_type_t;

typedef struct {
    mybot_ui_command_type_t type;
    char source_language[33];
    char target_language[33];
    /** Playback volume 0..100, or paused state 0/1, according to type. */
    int value;
} mybot_ui_command_t;

/** Borrowed immutable language list, valid for the lifetime of the process. */
MYBOT_API const mybot_ui_language_t *mybot_ui_languages(size_t *count);

/**
 * Copy a command into the bounded asynchronous control mailbox without waiting.
 * Returns 0 when accepted, -1 for invalid input, a full mailbox, contention or
 * a runtime which is not ready/is stopping. The caller retains its buffer.
 * Acceptance does not guarantee application: languages require idle state and
 * pause requires an active RTC session when the control owner applies it.
 * Observe LCD content for the actual languages, hardware volume and pause state.
 */
MYBOT_API int mybot_ui_submit(const mybot_ui_command_t *command);

#ifdef __cplusplus
}
#endif
#endif /* MYBOT_UI_H_ */
