/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_CAPTIONS_INTERNAL_H_
#define MYBOT_CAPTIONS_INTERNAL_H_

#include <mybot/mybot_captions.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MYBOT_CAPTION_MESSAGE_CAPACITY 8192U

typedef struct mybot_captions mybot_captions_t;

/* AOSL HAL allocations follow the board's PSRAM policy. No internal locking:
 * the control owner must serialize mutation and guard copies from UI readers. */
mybot_captions_t *mybot_captions_create(void);
void mybot_captions_destroy(mybot_captions_t *captions);
void mybot_captions_reset(mybot_captions_t *captions, uint32_t generation);
void mybot_captions_get_state(const mybot_captions_t *captions, mybot_caption_state_t *out);
int mybot_captions_copy(const mybot_captions_t *captions, uint32_t sequence, mybot_caption_t *out);

/* Apply one already-authenticated route message; NONE is invalid. Returns 1 for a visible
 * update, 0 for an ignored/duplicate event and -1 for invalid input or OOM.
 * Input is UTF-8 JSON with at most one optional final NUL, counted in len;
 * embedded NUL, more than 8192 bytes or nested containers beyond depth 16 fail.
 * IDs are nonempty strings up to 63 UTF-8 bytes or nonnegative signed-64-bit
 * integers, normalized to decimal strings. Missing final means interim; a
 * supplied final must be boolean. Optional relative times must be nonnegative
 * integers and are used only when both start_ms and duration_ms are present. */
int mybot_captions_apply_json(mybot_captions_t *captions, mybot_translation_direction_t direction,
                              const void *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* MYBOT_CAPTIONS_INTERNAL_H_ */
