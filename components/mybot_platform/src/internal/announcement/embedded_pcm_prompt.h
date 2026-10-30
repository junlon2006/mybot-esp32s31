/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <mybot/platform/mybot_audio.h>
int mybot_embedded_pcm_play_wifi_provisioning(const mybot_audio_playback_ops_t *playback,
                                              const mybot_audio_volume_ops_t *volume,
                                              const char *trigger);
