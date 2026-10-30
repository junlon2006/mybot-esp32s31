/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <mybot/platform/mybot_audio.h>
#ifdef __cplusplus
extern "C" {
#endif
const mybot_audio_capture_ops_t *mybot_korvo_s31_audio_capture_ops(void);
const mybot_audio_playback_ops_t *mybot_korvo_s31_audio_playback_ops(void);
const mybot_audio_volume_ops_t *mybot_korvo_s31_audio_volume_ops(void);
#ifdef __cplusplus
}
#endif
