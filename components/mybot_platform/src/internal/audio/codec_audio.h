/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <mybot/platform/mybot_audio.h>
#include "driver/i2s_common.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"

/* Board wiring and codec setup; stream ownership, bounded I/O and NVS volume
 * remain shared between boards. Only the selected board defines this object. */
typedef struct {
    const char *tag;
    unsigned bus_slots;
    unsigned mclk_multiple;
    int (*initialize_i2s)(i2s_chan_handle_t *tx_channel, i2s_chan_handle_t *rx_channel);
    int (*initialize_codec)(const audio_codec_ctrl_if_t **ctrl_if,
                            const audio_codec_if_t **codec_if);
    int (*configure_codec)(esp_codec_dev_handle_t device, int volume);
} mybot_codec_audio_board_t;
extern const mybot_codec_audio_board_t mybot_codec_audio_board;
const mybot_audio_capture_ops_t *mybot_codec_audio_capture_ops(void);
const mybot_audio_playback_ops_t *mybot_codec_audio_playback_ops(void);
const mybot_audio_volume_ops_t *mybot_codec_audio_volume_ops(void);
