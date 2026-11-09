/* SPDX-License-Identifier: Apache-2.0 */
#include "audio/mosaico_local_aec.h"

#include "esp_aec.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

#include <inttypes.h>
#include <stdbool.h>
#include <string.h>

#define LOCAL_AEC_TAG "mosaico_aec"
#define LOCAL_AEC_RATE 16000
#define LOCAL_AEC_FILTER_LENGTH 4
#define LOCAL_AEC_MAX_CHUNK 960
#define LOCAL_AEC_RING_FRAMES (LOCAL_AEC_MAX_CHUNK * 2)
#define LOCAL_AEC_LOG_INTERVAL_US (5LL * 1000 * 1000)

struct mosaico_local_aec {
    aec_handle_t *handle;
    int chunk_frames;
    int16_t *mic;
    int16_t *reference;
    int16_t *clean;
    int16_t *ring;
    size_t ring_head;
    size_t ring_count;
    uint32_t processed_chunks;
    uint32_t reference_chunks;
    int64_t max_process_us;
    int64_t last_log_us;
};

static int16_t *allocate_pcm(size_t frames) {
    size_t bytes = frames * sizeof(int16_t);
    int16_t *pcm = heap_caps_aligned_alloc(16, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (pcm)
        memset(pcm, 0, bytes);
    return pcm;
}

static int create_handle(mosaico_local_aec_t *aec) {
    aec_config_t config = {
        .mic_num = 1,
        .ref_num = 1,
        .out_num = 1,
        .filter_length = LOCAL_AEC_FILTER_LENGTH,
        .sample_rate = LOCAL_AEC_RATE,
        .caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
        .mode = AEC_MODE_FD_LOW_COST,
        .nlp_level = AEC_NLP_LEVEL_AGGR,
    };
    aec->handle = aec_create_from_config(&config);
    if (!aec->handle)
        return -1;
    int chunk = aec_get_chunksize(aec->handle);
    if (chunk <= 0 || chunk > LOCAL_AEC_MAX_CHUNK) {
        ESP_LOGE(LOCAL_AEC_TAG, "event=local_aec action=create result=error chunk=%d", chunk);
        return -1;
    }
    aec->chunk_frames = chunk;
    if (aec_set_nlp_level(aec->handle, AEC_NLP_LEVEL_AGGR) != AEC_NLP_LEVEL_AGGR)
        return -1;
    return 0;
}

static void release_handle(mosaico_local_aec_t *aec) {
    if (aec->handle)
        aec_destroy(aec->handle);
    aec->handle = NULL;
    aec->chunk_frames = 0;
}

static uint64_t frame_energy(const int16_t *pcm, size_t frames) {
    uint64_t energy = 0;
    for (size_t i = 0; i < frames; ++i)
        energy += (uint32_t)((int32_t)pcm[i] * pcm[i]);
    return energy;
}

static void log_stats(mosaico_local_aec_t *aec, bool reference_active) {
    if (reference_active)
        ++aec->reference_chunks;
    ++aec->processed_chunks;
    int64_t now = esp_timer_get_time();
    if (aec->last_log_us && now - aec->last_log_us >= LOCAL_AEC_LOG_INTERVAL_US) {
        ESP_LOGI(LOCAL_AEC_TAG,
                 "event=local_aec stats frames=%" PRIu32 " ref_active=%" PRIu32
                 " max_process_us=%" PRId64 " queued_ms=%u",
                 aec->processed_chunks, aec->reference_chunks, aec->max_process_us,
                 (unsigned)(aec->ring_count * 1000 / LOCAL_AEC_RATE));
        aec->processed_chunks = 0;
        aec->reference_chunks = 0;
        aec->max_process_us = 0;
        aec->last_log_us = now;
    }
}

static size_t ring_read(mosaico_local_aec_t *aec, int16_t *mono, size_t frames) {
    size_t copied = frames < aec->ring_count ? frames : aec->ring_count;
    size_t first = LOCAL_AEC_RING_FRAMES - aec->ring_head;
    if (first > copied)
        first = copied;
    memcpy(mono, aec->ring + aec->ring_head, first * sizeof(int16_t));
    if (copied > first)
        memcpy(mono + first, aec->ring, (copied - first) * sizeof(int16_t));
    aec->ring_head = (aec->ring_head + copied) % LOCAL_AEC_RING_FRAMES;
    aec->ring_count -= copied;
    return copied;
}

static int ring_append(mosaico_local_aec_t *aec, const int16_t *mono, size_t frames) {
    if (frames > LOCAL_AEC_RING_FRAMES - aec->ring_count)
        return -1;
    size_t tail = (aec->ring_head + aec->ring_count) % LOCAL_AEC_RING_FRAMES;
    size_t first = LOCAL_AEC_RING_FRAMES - tail;
    if (first > frames)
        first = frames;
    memcpy(aec->ring + tail, mono, first * sizeof(int16_t));
    if (frames > first)
        memcpy(aec->ring, mono + first, (frames - first) * sizeof(int16_t));
    aec->ring_count += frames;
    return 0;
}

mosaico_local_aec_t *mosaico_local_aec_create(void) {
    mosaico_local_aec_t *aec =
        heap_caps_calloc(1, sizeof(*aec), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!aec)
        return NULL;
    aec->mic = allocate_pcm(LOCAL_AEC_MAX_CHUNK);
    aec->reference = allocate_pcm(LOCAL_AEC_MAX_CHUNK);
    aec->clean = allocate_pcm(LOCAL_AEC_MAX_CHUNK);
    aec->ring = allocate_pcm(LOCAL_AEC_RING_FRAMES);
    if (!aec->mic || !aec->reference || !aec->clean || !aec->ring || create_handle(aec) < 0) {
        mosaico_local_aec_destroy(aec);
        return NULL;
    }
    aec->last_log_us = esp_timer_get_time();
    ESP_LOGI(LOCAL_AEC_TAG,
             "event=local_aec action=create mode=%s chunk=%d rate=%d filter=%d nlp=%s result=ok",
             aec_get_mode_string(AEC_MODE_FD_LOW_COST), aec->chunk_frames, LOCAL_AEC_RATE,
             LOCAL_AEC_FILTER_LENGTH, aec_get_nlp_string(AEC_NLP_LEVEL_AGGR));
    return aec;
}

void mosaico_local_aec_destroy(mosaico_local_aec_t *aec) {
    if (!aec)
        return;
    release_handle(aec);
    heap_caps_free(aec->mic);
    heap_caps_free(aec->reference);
    heap_caps_free(aec->clean);
    heap_caps_free(aec->ring);
    heap_caps_free(aec);
}

int mosaico_local_aec_reset(mosaico_local_aec_t *aec) {
    if (!aec)
        return -1;
    int expected_chunk = aec->chunk_frames;
    release_handle(aec);
    if (create_handle(aec) < 0)
        return -1;
    if (expected_chunk > 0 && aec->chunk_frames != expected_chunk) {
        release_handle(aec);
        return -1;
    }
    aec->ring_head = 0;
    aec->ring_count = 0;
    aec->processed_chunks = 0;
    aec->reference_chunks = 0;
    aec->max_process_us = 0;
    aec->last_log_us = esp_timer_get_time();
    return 0;
}

int mosaico_local_aec_chunk_frames(const mosaico_local_aec_t *aec) {
    return aec ? aec->chunk_frames : 0;
}

int mosaico_local_aec_feed(mosaico_local_aec_t *aec, const int16_t *interleaved, size_t frames) {
    if (!aec || !aec->handle || !interleaved || frames != (size_t)aec->chunk_frames)
        return -1;
    for (size_t i = 0; i < frames; ++i) {
        aec->mic[i] = interleaved[i * 2];
        aec->reference[i] = interleaved[i * 2 + 1];
    }
    int64_t started = esp_timer_get_time();
    aec_process(aec->handle, aec->mic, aec->reference, aec->clean);
    int64_t elapsed = esp_timer_get_time() - started;
    if (elapsed > aec->max_process_us)
        aec->max_process_us = elapsed;
    if (ring_append(aec, aec->clean, frames) < 0) {
        ESP_LOGE(LOCAL_AEC_TAG,
                 "event=local_aec action=queue result=overflow frames=%u capacity=%u",
                 (unsigned)frames, (unsigned)aec->ring_count);
        return -1;
    }
    bool reference_active = frame_energy(aec->reference, frames) > (uint64_t)frames * 256;
    log_stats(aec, reference_active);
    return 0;
}

size_t mosaico_local_aec_read(mosaico_local_aec_t *aec, int16_t *mono, size_t frames) {
    return aec && mono ? ring_read(aec, mono, frames) : 0;
}
