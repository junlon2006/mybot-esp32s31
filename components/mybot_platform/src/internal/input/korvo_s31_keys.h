/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_KORVO_S31_KEYS_H_
#define MYBOT_KORVO_S31_KEYS_H_

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Called only by the LVGL task, including valid idle samples. An invalid sample
 * cancels the gesture until the next valid release. This never waits for an SDK
 * callback or calls conversation APIs: the board key worker delivers the event. */
void mybot_korvo_s31_keys_touch_sample(bool valid, bool pressed);

#ifdef __cplusplus
}
#endif

#endif /* MYBOT_KORVO_S31_KEYS_H_ */
