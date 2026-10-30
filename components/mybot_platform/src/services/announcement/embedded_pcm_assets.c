/* SPDX-License-Identifier: Apache-2.0 */
#include "announcement/embedded_pcm_prompt.h"
#include "audio/pcm_playback_buffer.h"
#include <mybot/platform/mybot_announce.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define DECLARE_ASSET(name, file)                                                                  \
    extern const uint8_t name##_start[] asm("_binary_" #file "_pcm_start");                        \
    extern const uint8_t name##_end[] asm("_binary_" #file "_pcm_end")
DECLARE_ASSET(prompt, prompt);
DECLARE_ASSET(wificonfig, wificonfig);
DECLARE_ASSET(digit_0, 0);
DECLARE_ASSET(digit_1, 1);
DECLARE_ASSET(digit_2, 2);
DECLARE_ASSET(digit_3, 3);
DECLARE_ASSET(digit_4, 4);
DECLARE_ASSET(digit_5, 5);
DECLARE_ASSET(digit_6, 6);
DECLARE_ASSET(digit_7, 7);
DECLARE_ASSET(digit_8, 8);
DECLARE_ASSET(digit_9, 9);

typedef struct {
    const uint8_t *begin;
    const uint8_t *end;
} pcm_asset_t;
typedef struct {
    const pcm_asset_t *asset;
    size_t offset;
} pcm_sound_t;
#define ASSET(name)                                                                                \
    { .begin = name##_start, .end = name##_end }
static const pcm_asset_t s_assets[MYBOT_ANNOUNCE_SOUND_COUNT] = {
    [MYBOT_ANNOUNCE_SOUND_PROMPT] = ASSET(prompt),
    [MYBOT_ANNOUNCE_SOUND_DIGIT_0] = ASSET(digit_0),
    [MYBOT_ANNOUNCE_SOUND_DIGIT_1] = ASSET(digit_1),
    [MYBOT_ANNOUNCE_SOUND_DIGIT_2] = ASSET(digit_2),
    [MYBOT_ANNOUNCE_SOUND_DIGIT_3] = ASSET(digit_3),
    [MYBOT_ANNOUNCE_SOUND_DIGIT_4] = ASSET(digit_4),
    [MYBOT_ANNOUNCE_SOUND_DIGIT_5] = ASSET(digit_5),
    [MYBOT_ANNOUNCE_SOUND_DIGIT_6] = ASSET(digit_6),
    [MYBOT_ANNOUNCE_SOUND_DIGIT_7] = ASSET(digit_7),
    [MYBOT_ANNOUNCE_SOUND_DIGIT_8] = ASSET(digit_8),
    [MYBOT_ANNOUNCE_SOUND_DIGIT_9] = ASSET(digit_9),
};
static const pcm_asset_t s_wifi = ASSET(wificonfig);

static int announce_init(void **out) {
    if (!out)
        return -1;
    *out = heap_caps_calloc(1, 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return *out ? 0 : -1;
}
static void *announce_open(void *ctx, mybot_announce_sound_t sound) {
    if (!ctx || sound < 0 || sound >= MYBOT_ANNOUNCE_SOUND_COUNT)
        return NULL;
    pcm_sound_t *handle = heap_caps_calloc(1, sizeof(*handle), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (handle)
        handle->asset = &s_assets[sound];
    return handle;
}
static int announce_read(void *ctx, void *sound, int16_t *dst, int frames) {
    pcm_sound_t *handle = sound;
    if (!ctx || !handle || !dst || frames <= 0)
        return 0;
    size_t total = (size_t)(handle->asset->end - handle->asset->begin) / sizeof(int16_t);
    size_t count = total - handle->offset;
    if (count > (size_t)frames)
        count = (size_t)frames;
    memcpy(dst, handle->asset->begin + handle->offset * sizeof(int16_t), count * sizeof(int16_t));
    handle->offset += count;
    return (int)count;
}
static void announce_close(void *ctx, void *sound) {
    (void)ctx;
    heap_caps_free(sound);
}
static void announce_destroy(void *ctx) {
    heap_caps_free(ctx);
}
static const mybot_announce_ops_t s_announce = {
    .init = announce_init,
    .open = announce_open,
    .read = announce_read,
    .close = announce_close,
    .destroy = announce_destroy,
};
const mybot_announce_ops_t *mybot_korvo_s31_announce_ops(void) {
    return &s_announce;
}

int mybot_embedded_pcm_play_wifi_provisioning(const mybot_audio_playback_ops_t *playback,
                                              const mybot_audio_volume_ops_t *volume,
                                              const char *trigger) {
    if (!playback || !playback->init || !playback->start || !playback->write || !playback->stop ||
        !playback->destroy)
        return -1;
    void *audio = NULL, *volume_ctx = NULL;
    bool started = false, volume_ready = false;
    int result = -1;
    int16_t *scratch = heap_caps_malloc(320 * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!scratch)
        return -1;
    if (playback->init(&audio, 16000, 1, 16) < 0)
        goto cleanup;
    if (volume && volume->init && volume->destroy) {
        if (volume->init(&volume_ctx) < 0)
            goto cleanup;
        volume_ready = true;
    }
    if (playback->start(audio) < 0)
        goto cleanup;
    started = true;
    size_t total = (size_t)(s_wifi.end - s_wifi.begin) / sizeof(int16_t);
    size_t offset = 0;
    unsigned timeouts = 0;
    while (offset < total) {
        int count = (int)(total - offset);
        if (count > 320)
            count = 320;
        memcpy(scratch, s_wifi.begin + offset * sizeof(int16_t), (size_t)count * sizeof(int16_t));
        int written = playback->write(audio, scratch, count);
        if (written < 0 || written > count)
            goto cleanup;
        if (written == 0) {
            if (++timeouts >= 100)
                goto cleanup;
            continue;
        }
        timeouts = 0;
        offset += (size_t)written;
    }
    result = mybot_audio_playback_drain(audio, 1000);
cleanup:
    if (started && playback->stop(audio) < 0)
        result = -1;
    if (audio)
        playback->destroy(audio);
    if (volume_ready)
        volume->destroy(volume_ctx);
    heap_caps_free(scratch);
    ESP_LOGI("mybot_prompt", "event=prompt trigger=%s result=%s format=pcm16 language=%s",
             trigger ? trigger : "unknown", result == 0 ? "ok" : "error",
             CONFIG_MYBOT_LANGUAGE_TAG);
    return result;
}
