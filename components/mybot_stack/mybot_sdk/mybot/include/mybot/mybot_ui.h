/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_UI_H_
#define MYBOT_UI_H_

#include <mybot/mybot_export.h>
#include <mybot/mybot_translation.h>
#include <mybot/platform/mybot_audio.h>
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
    MYBOT_UI_SET_MODE,
    MYBOT_UI_SET_DIRECTION,
} mybot_ui_command_type_t;

typedef struct {
    mybot_ui_command_type_t type;
    char source_language[33];
    char target_language[33];
    /** Volume 0..100, paused 0/1, translation mode, or listen/speak direction. */
    int value;
    /** SET_MODE selects mode and source together. USB supports simultaneous LISTEN_SPEAK.
     * Optional nonempty language fields select that scene's pair (my/other for LISTEN_SPEAK);
     * both empty preserve the current pair. Other commands ignore this field. */
    mybot_audio_source_t audio_source;
} mybot_ui_command_t;

/** Borrowed immutable language list, valid for the lifetime of the process. */
MYBOT_API const mybot_ui_language_t *mybot_ui_languages(size_t *count);

/**
 * Copy a command into the bounded asynchronous control mailbox without waiting.
 * Returns 0 when accepted, -1 for invalid input, a full mailbox, contention or
 * a runtime which is not ready/is stopping. The caller retains its buffer.
 * Acceptance does not guarantee application: languages/mode/source require idle state;
 * pause and direction require an active session when the control owner applies
 * the command. Direction applies only to face-to-face and resumes local audio.
 * Observe LCD content for the actual languages, mode, source, direction, volume and pause.
 */
MYBOT_API int mybot_ui_submit(const mybot_ui_command_t *command);

#ifdef __cplusplus
}
#endif
#endif /* MYBOT_UI_H_ */
