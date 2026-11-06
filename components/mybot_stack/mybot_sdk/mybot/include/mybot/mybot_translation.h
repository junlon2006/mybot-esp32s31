/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_TRANSLATION_H_
#define MYBOT_TRANSLATION_H_

/** Media uses one listen route; face-to-face uses listen and speak together. */
typedef enum {
    MYBOT_TRANSLATION_LISTEN = 0,
    MYBOT_TRANSLATION_LISTEN_SPEAK = 1,
} mybot_translation_mode_t;

/** NONE keeps microphone content closed until the user chooses a direction. */
typedef enum {
    MYBOT_TRANSLATION_DIRECTION_NONE = 0,
    MYBOT_TRANSLATION_DIRECTION_LISTEN = 1,
    MYBOT_TRANSLATION_DIRECTION_SPEAK = 2,
} mybot_translation_direction_t;

#endif /* MYBOT_TRANSLATION_H_ */
