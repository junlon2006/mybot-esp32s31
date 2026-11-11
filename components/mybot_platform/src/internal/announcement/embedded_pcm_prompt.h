/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_EMBEDDED_PCM_PROMPT_H_
#define MYBOT_EMBEDDED_PCM_PROMPT_H_

#include <mybot/platform/mybot_audio.h>

#ifdef __cplusplus
extern "C" {
#endif

int mybot_embedded_pcm_play_wifi_provisioning(const mybot_audio_playback_ops_t *playback,
                                              const mybot_audio_volume_ops_t *volume,
                                              const char *trigger);

#ifdef __cplusplus
}
#endif

#endif /* MYBOT_EMBEDDED_PCM_PROMPT_H_ */
