/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_MOSAICO_CODEC_AUDIO_H_
#define MYBOT_MOSAICO_CODEC_AUDIO_H_

#include <mybot/platform/mybot_audio.h>

#ifdef __cplusplus
extern "C" {
#endif

const mybot_audio_capture_ops_t *mybot_mosaico_audio_capture_ops(void);
const mybot_audio_playback_ops_t *mybot_mosaico_audio_playback_ops(void);
const mybot_audio_volume_ops_t *mybot_mosaico_audio_volume_ops(void);

#ifdef __cplusplus
}
#endif

#endif /* MYBOT_MOSAICO_CODEC_AUDIO_H_ */
