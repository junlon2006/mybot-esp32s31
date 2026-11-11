/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_TEST_CODEC_AUDIO_MOCKS_ESP_AEC_H_
#define MYBOT_TEST_CODEC_AUDIO_MOCKS_ESP_AEC_H_

#include <stdint.h>

typedef enum {
    AEC_MODE_SR_LOW_COST = 0,
    AEC_MODE_SR_HIGH_PERF = 1,
    AEC_MODE_VOIP_LOW_COST = 3,
    AEC_MODE_VOIP_HIGH_PERF = 4,
    AEC_MODE_FD_LOW_COST = 5,
    AEC_MODE_FD_HIGH_PERF = 6,
} aec_mode_t;

typedef enum {
    AEC_NLP_LEVEL_NORMAL = 0,
    AEC_NLP_LEVEL_AGGR = 1,
    AEC_NLP_LEVEL_VERYAGGR = 2,
} aec_nlp_level_t;

typedef struct {
    int mic_num;
    int ref_num;
    int out_num;
    int filter_length;
    int sample_rate;
    uint32_t caps;
    aec_mode_t mode;
    aec_nlp_level_t nlp_level;
} aec_config_t;

typedef struct {
    void *aec_handle;
    int frame_size;
    aec_config_t config;
} aec_handle_t;

aec_handle_t *aec_create_from_config(aec_config_t *config);
void aec_process(const aec_handle_t *handle, int16_t *indata, int16_t *refdata, int16_t *outdata);
aec_nlp_level_t aec_set_nlp_level(const aec_handle_t *handle, aec_nlp_level_t level);
int aec_get_chunksize(const aec_handle_t *handle);
char *aec_get_mode_string(aec_mode_t mode);
char *aec_get_nlp_string(aec_nlp_level_t level);
void aec_destroy(aec_handle_t *handle);

#endif /* MYBOT_TEST_CODEC_AUDIO_MOCKS_ESP_AEC_H_ */
