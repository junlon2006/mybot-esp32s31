/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_CAPTIONS_H_
#define MYBOT_CAPTIONS_H_

#include <mybot/mybot_export.h>
#include <mybot/mybot_translation.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** UTF-8 byte capacity per side, including the terminating NUL. */
#define MYBOT_CAPTION_TEXT_CAPACITY 1024U
#define MYBOT_CAPTION_HISTORY_CAPACITY 64U

typedef struct {
    uint32_t sequence;
    mybot_translation_direction_t direction;
    char source_text[MYBOT_CAPTION_TEXT_CAPACITY];
    char target_text[MYBOT_CAPTION_TEXT_CAPACITY];
    bool source_final;
    bool target_final;
    bool source_truncated;
    bool target_truncated;
} mybot_caption_t;

typedef struct {
    uint32_t generation;
    uint32_t revision;
    uint32_t count;
    uint32_t first_sequence;
    uint32_t latest_sequence;
    uint32_t latest_listen_sequence;
    uint32_t latest_speak_sequence;
} mybot_caption_state_t;

/**
 * Copy one retained caption without waiting for the control owner. The sequence
 * must be nonzero and belong to expected_generation. Returns 0 on success or -1
 * when busy, absent or reset. The caller owns the output; failed reads leave it
 * unchanged. Final text rejects later interim revisions of the same side.
 */
MYBOT_API int mybot_ui_copy_caption(uint32_t expected_generation, uint32_t sequence,
                                    mybot_caption_t *out);

#ifdef __cplusplus
}
#endif
#endif /* MYBOT_CAPTIONS_H_ */
