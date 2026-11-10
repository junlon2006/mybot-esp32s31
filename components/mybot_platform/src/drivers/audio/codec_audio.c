/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2025 Project Contributors */
#include "board_config.h"
#include "audio/codec_audio.h"
#if MYBOT_LOCAL_AEC
#include "audio/mosaico_local_aec.h"
#endif
#include "audio/pcm_playback_buffer.h"

#include "driver/gpio.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#ifndef MYBOT_LOCAL_AEC
#define MYBOT_LOCAL_AEC 0
#endif

#ifndef MYBOT_CLOUD_AEC
#define MYBOT_CLOUD_AEC 0
#endif

#if MYBOT_LOCAL_AEC && MYBOT_CLOUD_AEC
#error "Local and Cloud AEC are mutually exclusive"
#endif

#define TAG mybot_codec_audio_board.tag
#define AUDIO_CONTEXT_MAGIC 0x53333141U
#define AUDIO_IO_TIMEOUT_MS 50
#define AUDIO_LOCK_TIMEOUT_MS 1500
#define AUDIO_STOP_TIMEOUT_MS 150
#define AUDIO_MAX_FRAMES 960
#define AUDIO_WRITE_FRAMES 240
#define AUDIO_BUS_SLOTS mybot_codec_audio_board.bus_slots
#define AUDIO_VOLUME_NVS_KEY "output_volume"
#define AUDIO_VOLUME_NVS_NAMESPACE "audio"
typedef enum { AUDIO_DIRECTION_CAPTURE, AUDIO_DIRECTION_PLAYBACK } audio_direction_t;
typedef struct audio_stream_context {
    uint32_t magic;
    audio_direction_t direction;
    bool started;
    unsigned in_flight;
    int16_t *scratch;
    mybot_pcm_playback_buffer_t *playback_buffer;
    bool stop_pending;
    bool deferred;
    struct audio_stream_context *deferred_next;
#if MYBOT_LOCAL_AEC
    mosaico_local_aec_t *local_aec;
    size_t local_aec_input_frames;
#endif
} audio_stream_context_t;
typedef struct {
    i2s_chan_handle_t tx_channel;
    i2s_chan_handle_t rx_channel;
    const audio_codec_ctrl_if_t *ctrl_if;
    const audio_codec_if_t *codec_if;
    esp_codec_dev_handle_t device;
    unsigned references;
    bool ready;
    bool codec_open;
    bool tx_enabled;
    bool rx_enabled;
    bool capture_started;
    bool playback_started;
} codec_audio_shared_t;
typedef struct {
    nvs_handle_t handle;
    bool active;
    bool persisted_volume_known;
    bool persist_dirty;
    int persisted_volume;
} audio_volume_context_t;

static codec_audio_shared_t s_audio;
static audio_stream_context_t *s_deferred;
static bool reap_deferred_locked(void);
static audio_volume_context_t s_volume_context;
static int s_output_volume = MYBOT_AUDIO_DEFAULT_VOLUME;
static StaticSemaphore_t s_audio_mutex_storage;
static SemaphoreHandle_t s_audio_mutex;
static portMUX_TYPE s_audio_state_lock = portMUX_INITIALIZER_UNLOCKED;

/* The codec library controls registers only. All PCM I/O below has a finite timeout. */
static bool codec_data_is_open(const audio_codec_data_if_t *iface) {
    return iface != NULL;
}
static int codec_data_enable(const audio_codec_data_if_t *iface, esp_codec_dev_type_t type,
                             bool enable) {
    (void)iface;
    (void)type;
    (void)enable;
    return ESP_CODEC_DEV_OK;
}
static int codec_data_set_format(const audio_codec_data_if_t *iface, esp_codec_dev_type_t type,
                                 esp_codec_dev_sample_info_t *format) {
    (void)iface;
    (void)type;
    return format && format->sample_rate == MYBOT_AUDIO_SAMPLE_RATE &&
                   format->bits_per_sample == 16 && format->channel == AUDIO_BUS_SLOTS
               ? ESP_CODEC_DEV_OK
               : ESP_CODEC_DEV_NOT_SUPPORT;
}
static int codec_data_read(const audio_codec_data_if_t *iface, uint8_t *data, int size) {
    (void)iface;
    (void)data;
    (void)size;
    return ESP_CODEC_DEV_NOT_SUPPORT;
}
static int codec_data_write(const audio_codec_data_if_t *iface, uint8_t *data, int size) {
    (void)iface;
    (void)data;
    (void)size;
    return ESP_CODEC_DEV_NOT_SUPPORT;
}
static const audio_codec_data_if_t s_codec_data_if = {
    .is_open = codec_data_is_open,
    .enable = codec_data_enable,
    .set_fmt = codec_data_set_format,
    .read = codec_data_read,
    .write = codec_data_write,
};

static bool audio_lock(void) {
    portENTER_CRITICAL(&s_audio_state_lock);
    if (!s_audio_mutex)
        s_audio_mutex = xSemaphoreCreateMutexStatic(&s_audio_mutex_storage);
    SemaphoreHandle_t mutex = s_audio_mutex;
    portEXIT_CRITICAL(&s_audio_state_lock);
    return mutex && xSemaphoreTake(mutex, pdMS_TO_TICKS(AUDIO_LOCK_TIMEOUT_MS)) == pdTRUE;
}
static void audio_unlock(void) {
    xSemaphoreGive(s_audio_mutex);
}
static const char *direction_name(audio_direction_t direction) {
    return direction == AUDIO_DIRECTION_CAPTURE ? "capture" : "playback";
}
static bool context_valid(audio_stream_context_t *context, audio_direction_t direction) {
    return context && context->magic == AUDIO_CONTEXT_MAGIC && context->direction == direction;
}
static bool stream_running(audio_stream_context_t *context) {
    return __atomic_load_n(&context->started, __ATOMIC_ACQUIRE);
}
static int set_pa(bool enabled) {
    return gpio_set_level(MYBOT_AUDIO_PA_GPIO, enabled ? 1 : 0) == ESP_OK ? 0 : -1;
}
static int disable_channel(i2s_chan_handle_t channel, bool *enabled) {
    if (!channel || !*enabled)
        return 0;
    if (i2s_channel_disable(channel) != ESP_OK)
        return -1;
    *enabled = false;
    return 0;
}
static int close_codec_locked(void) {
    if (!s_audio.codec_open)
        return 0;
    if (esp_codec_dev_close(s_audio.device) != ESP_CODEC_DEV_OK)
        return -1;
    s_audio.codec_open = false;
    return 0;
}
static int shared_release_locked(void) {
    s_audio.ready = false;
    if (set_pa(false) < 0 || close_codec_locked() < 0 ||
        disable_channel(s_audio.rx_channel, &s_audio.rx_enabled) < 0 ||
        disable_channel(s_audio.tx_channel, &s_audio.tx_enabled) < 0)
        return -1;
    if (s_audio.rx_channel) {
        if (i2s_del_channel(s_audio.rx_channel) != ESP_OK)
            return -1;
        s_audio.rx_channel = NULL;
    }
    if (s_audio.tx_channel) {
        if (i2s_del_channel(s_audio.tx_channel) != ESP_OK)
            return -1;
        s_audio.tx_channel = NULL;
    }
    if (s_audio.device) {
        esp_codec_dev_delete(s_audio.device);
        s_audio.device = NULL;
    }
    if (s_audio.codec_if) {
        if (audio_codec_delete_codec_if(s_audio.codec_if) != ESP_CODEC_DEV_OK)
            return -1;
        s_audio.codec_if = NULL;
    }
    if (s_audio.ctrl_if) {
        if (audio_codec_delete_ctrl_if(s_audio.ctrl_if) != ESP_CODEC_DEV_OK)
            return -1;
        s_audio.ctrl_if = NULL;
    }
    s_audio = (codec_audio_shared_t){0};
    return 0;
}
static int shared_acquire_locked(void) {
    if (s_audio.ready) {
        ++s_audio.references;
        return 0;
    }
    if (s_audio.references) {
        /* A context still owns a failed teardown. It must retry destroy before
         * any new stream can acquire the partially released hardware. */
        return -1;
    }
    /* An earlier partial cleanup is retried before new resources are allocated. */
    if (shared_release_locked() < 0 ||
        mybot_codec_audio_board.initialize_i2s(&s_audio.tx_channel, &s_audio.rx_channel) < 0 ||
        mybot_codec_audio_board.initialize_codec(&s_audio.ctrl_if, &s_audio.codec_if) < 0) {
        (void)shared_release_locked();
        return -1;
    }
    esp_codec_dev_cfg_t device = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN_OUT,
        .codec_if = s_audio.codec_if,
        .data_if = &s_codec_data_if,
    };
    s_audio.device = esp_codec_dev_new(&device);
    if (!s_audio.device) {
        (void)shared_release_locked();
        return -1;
    }
    s_audio.references = 1;
    s_audio.ready = true;
    ESP_LOGI(TAG,
             "event=audio_device action=initialize rate=%d slots=%u bits=16 "
             "bclk_hz=%u capture_slot=0 result=ok",
             MYBOT_AUDIO_SAMPLE_RATE, AUDIO_BUS_SLOTS,
             MYBOT_AUDIO_SAMPLE_RATE * AUDIO_BUS_SLOTS * 16);
    return 0;
}
static int start_codec_locked(void) {
    if (s_audio.codec_open)
        return 0;
    esp_codec_dev_sample_info_t format = {
        .bits_per_sample = 16,
        .channel = AUDIO_BUS_SLOTS,
        .sample_rate = MYBOT_AUDIO_SAMPLE_RATE,
        .mclk_multiple = mybot_codec_audio_board.mclk_multiple,
    };
    /* open() can leave partially enabled state on failure; close remains mandatory. */
    s_audio.codec_open = true;
    if (esp_codec_dev_open(s_audio.device, &format) != ESP_CODEC_DEV_OK ||
        mybot_codec_audio_board.configure_codec(s_audio.device, s_output_volume) < 0) {
        (void)close_codec_locked();
        return -1;
    }
#if MYBOT_LOCAL_AEC
    int reference_config = 0;
    if (esp_codec_dev_read_reg(s_audio.device, 0x44, &reference_config) != ESP_CODEC_DEV_OK ||
        (reference_config & 0x70) != 0x50) {
        ESP_LOGE(TAG, "event=local_aec action=reference_check result=error reg44=0x%02x",
                 reference_config);
        (void)close_codec_locked();
        return -1;
    }
#endif
    return 0;
}
static int ensure_tx_locked(void) {
    if (s_audio.tx_enabled)
        return 0;
    if (i2s_channel_enable(s_audio.tx_channel) != ESP_OK)
        return -1;
    s_audio.tx_enabled = true;
    return 0;
}
static esp_err_t playback_write_i2s(void *opaque, const int16_t *pcm, size_t frames,
                                    size_t *written_frames, uint32_t timeout_ms) {
    audio_stream_context_t *context = opaque;
    *written_frames = 0;
    if (frames > AUDIO_WRITE_FRAMES)
        return ESP_ERR_INVALID_ARG;
    for (size_t frame = 0; frame < frames; ++frame)
        for (unsigned slot = 0; slot < AUDIO_BUS_SLOTS; ++slot)
            context->scratch[frame * AUDIO_BUS_SLOTS + slot] = pcm[frame];
    size_t bytes = 0;
    esp_err_t result =
        i2s_channel_write(s_audio.tx_channel, context->scratch,
                          frames * AUDIO_BUS_SLOTS * sizeof(int16_t), &bytes, timeout_ms);
    if (bytes > frames * AUDIO_BUS_SLOTS * sizeof(int16_t) ||
        bytes % (AUDIO_BUS_SLOTS * sizeof(int16_t)))
        return ESP_FAIL;
    *written_frames = bytes / (AUDIO_BUS_SLOTS * sizeof(int16_t));
    return result;
}
static int stream_init(void **out, int rate, int channels, int bits, audio_direction_t direction) {
    if (!out)
        return -1;
    *out = NULL;
    if (rate != MYBOT_AUDIO_SAMPLE_RATE || channels != 1 || bits != 16)
        return -1;
    audio_stream_context_t *context =
        heap_caps_calloc(1, sizeof(*context), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!context)
        return -1;
    context->magic = AUDIO_CONTEXT_MAGIC;
    context->direction = direction;
    size_t frames = direction == AUDIO_DIRECTION_CAPTURE ? AUDIO_MAX_FRAMES : AUDIO_WRITE_FRAMES;
    context->scratch = heap_caps_calloc(frames * AUDIO_BUS_SLOTS, sizeof(int16_t),
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!context->scratch || !audio_lock())
        goto failed;
    int result = reap_deferred_locked() ? shared_acquire_locked() : -1;
    if (result == 0 && direction == AUDIO_DIRECTION_PLAYBACK) {
        context->playback_buffer = mybot_pcm_playback_buffer_create(playback_write_i2s, context);
        if (!context->playback_buffer) {
            if (--s_audio.references == 0)
                (void)shared_release_locked();
            result = -1;
        }
    }
    audio_unlock();
    if (result < 0)
        goto failed;
    *out = context;
    return 0;
failed:
    free(context->scratch);
    free(context);
    return -1;
}
static int capture_init(void **out, int rate, int channels, int bits) {
    return stream_init(out, rate, channels, bits, AUDIO_DIRECTION_CAPTURE);
}
static int playback_init(void **out, int rate, int channels, int bits) {
    return stream_init(out, rate, channels, bits, AUDIO_DIRECTION_PLAYBACK);
}
#if MYBOT_LOCAL_AEC
static int local_capture_start(audio_stream_context_t *context) {
    if (context->local_aec)
        return 0;
    context->local_aec = mosaico_local_aec_create();
    context->local_aec_input_frames = 0;
    return context->local_aec ? 0 : -1;
}
static void local_capture_release(audio_stream_context_t *context) {
    mosaico_local_aec_destroy(context->local_aec);
    context->local_aec = NULL;
    context->local_aec_input_frames = 0;
}
static int local_capture_reset(audio_stream_context_t *context) {
    if (!context->local_aec)
        return local_capture_start(context);
    context->local_aec_input_frames = 0;
    return mosaico_local_aec_reset(context->local_aec);
}
static int local_capture_fill(audio_stream_context_t *context) {
    int chunk = mosaico_local_aec_chunk_frames(context->local_aec);
    if (chunk <= 0 || chunk > AUDIO_MAX_FRAMES)
        return -1;
    if (!stream_running(context))
        return 0;
    size_t remaining = (size_t)chunk - context->local_aec_input_frames;
    uint32_t timeout =
        AUDIO_IO_TIMEOUT_MS + (uint32_t)(remaining * 1000U / MYBOT_AUDIO_SAMPLE_RATE);
    size_t bytes = 0;
    esp_err_t err = i2s_channel_read(
        s_audio.rx_channel, context->scratch + context->local_aec_input_frames * AUDIO_BUS_SLOTS,
        remaining * AUDIO_BUS_SLOTS * sizeof(int16_t), &bytes, timeout);
    bool valid = bytes <= remaining * AUDIO_BUS_SLOTS * sizeof(int16_t) &&
                 bytes % (AUDIO_BUS_SLOTS * sizeof(int16_t)) == 0;
    int captured = valid ? (int)(bytes / (AUDIO_BUS_SLOTS * sizeof(int16_t))) : -1;
    if (err != ESP_OK && err != ESP_ERR_TIMEOUT && stream_running(context))
        return -1;
    if (captured < 0)
        return -1;
    if (captured == 0)
        return 0;
    context->local_aec_input_frames += (size_t)captured;
    if (context->local_aec_input_frames < (size_t)chunk)
        return 0;
    int result = mosaico_local_aec_feed(context->local_aec, context->scratch, (size_t)chunk);
    context->local_aec_input_frames = 0;
    return result;
}
static int capture_read_aec(audio_stream_context_t *context, int16_t *output, int frames) {
    size_t produced = mosaico_local_aec_read(context->local_aec, output, (size_t)frames);
    while (produced < (size_t)frames && stream_running(context)) {
        if (local_capture_fill(context) < 0)
            return -1;
        size_t copied = mosaico_local_aec_read(context->local_aec, output + produced,
                                               (size_t)frames - produced);
        if (!copied)
            break;
        produced += copied;
    }
    return (int)produced;
}
#endif
static int capture_start(void *opaque) {
    audio_stream_context_t *context = opaque;
    if (!context_valid(context, AUDIO_DIRECTION_CAPTURE) || !audio_lock())
        return -1;
    int result = -1;
    if (!s_audio.ready || stream_running(context) || context->stop_pending ||
        s_audio.capture_started)
        goto done;
    if (ensure_tx_locked() < 0 || start_codec_locked() < 0)
        goto rollback;
#if MYBOT_LOCAL_AEC
    if (local_capture_start(context) < 0)
        goto rollback;
#endif
    if (i2s_channel_enable(s_audio.rx_channel) != ESP_OK)
        goto rollback;
    s_audio.rx_enabled = true;
    s_audio.capture_started = true;
    __atomic_store_n(&context->started, true, __ATOMIC_RELEASE);
    result = 0;
    goto done;
rollback:
#if MYBOT_LOCAL_AEC
    local_capture_release(context);
#endif
    if (!s_audio.playback_started) {
        context->stop_pending = close_codec_locked() < 0;
        context->stop_pending |= disable_channel(s_audio.tx_channel, &s_audio.tx_enabled) < 0;
    }
done:
    audio_unlock();
    return result;
}
static int playback_start(void *opaque) {
    audio_stream_context_t *context = opaque;
    if (!context_valid(context, AUDIO_DIRECTION_PLAYBACK) || !audio_lock())
        return -1;
    int result = -1;
    if (!s_audio.ready || stream_running(context) || context->stop_pending ||
        s_audio.playback_started)
        goto done;
    if (ensure_tx_locked() < 0 || start_codec_locked() < 0 ||
        esp_codec_dev_set_out_vol(s_audio.device, s_output_volume) != ESP_CODEC_DEV_OK ||
        mybot_pcm_playback_buffer_start(context->playback_buffer) < 0)
        goto rollback;
    if (set_pa(true) < 0) {
        if (mybot_pcm_playback_buffer_stop(context->playback_buffer) < 0)
            context->stop_pending = true;
        goto rollback;
    }
    s_audio.playback_started = true;
    __atomic_store_n(&context->started, true, __ATOMIC_RELEASE);
    result = 0;
    goto done;
rollback:
    (void)set_pa(false);
    if (!s_audio.capture_started && !context->stop_pending) {
        context->stop_pending = close_codec_locked() < 0;
        context->stop_pending |= disable_channel(s_audio.tx_channel, &s_audio.tx_enabled) < 0;
    }
done:
    audio_unlock();
    return result;
}
static int capture_read(void *opaque, void *buffer, int frames) {
    audio_stream_context_t *context = opaque;
    if (!context_valid(context, AUDIO_DIRECTION_CAPTURE) || !buffer || frames <= 0 ||
        frames > AUDIO_MAX_FRAMES)
        return -1;
    portENTER_CRITICAL(&s_audio_state_lock);
    bool running = stream_running(context);
    if (running)
        ++context->in_flight;
    portEXIT_CRITICAL(&s_audio_state_lock);
    if (!running)
        return 0;
#if MYBOT_LOCAL_AEC
    int captured = capture_read_aec(context, buffer, frames);
#else
    size_t bytes = 0;
    esp_err_t err = i2s_channel_read(s_audio.rx_channel, context->scratch,
                                     (size_t)frames * AUDIO_BUS_SLOTS * sizeof(int16_t), &bytes,
                                     AUDIO_IO_TIMEOUT_MS);
    bool valid = bytes <= (size_t)frames * AUDIO_BUS_SLOTS * sizeof(int16_t) &&
                 bytes % (AUDIO_BUS_SLOTS * sizeof(int16_t)) == 0;
    int captured = valid ? (int)(bytes / (AUDIO_BUS_SLOTS * sizeof(int16_t))) : -1;
    if (err != ESP_OK && err != ESP_ERR_TIMEOUT)
        captured = stream_running(context) ? -1 : 0;
    if (captured > 0) {
        int16_t *output = buffer;
        for (int frame = 0; frame < captured; ++frame)
            output[frame] = context->scratch[frame * AUDIO_BUS_SLOTS];
    }
#endif
    portENTER_CRITICAL(&s_audio_state_lock);
    --context->in_flight;
    portEXIT_CRITICAL(&s_audio_state_lock);
    return captured;
}
static int playback_write(void *opaque, const void *buffer, int frames) {
    audio_stream_context_t *context = opaque;
    if (!context_valid(context, AUDIO_DIRECTION_PLAYBACK) || !buffer || frames <= 0 ||
        frames > AUDIO_MAX_FRAMES)
        return -1;
    return stream_running(context)
               ? mybot_pcm_playback_buffer_write(context->playback_buffer, buffer, frames)
               : 0;
}
int mybot_audio_playback_drain(void *opaque, uint32_t timeout_ms) {
    audio_stream_context_t *context = opaque;
    if (!context_valid(context, AUDIO_DIRECTION_PLAYBACK) || !audio_lock())
        return -1;
    int result = mybot_pcm_playback_buffer_drain(context->playback_buffer, timeout_ms);
    audio_unlock();
    return result;
}
static int stream_stop_locked(audio_stream_context_t *context) {
    if (!stream_running(context) && !context->stop_pending)
        return 0;
    portENTER_CRITICAL(&s_audio_state_lock);
    __atomic_store_n(&context->started, false, __ATOMIC_RELEASE);
    portEXIT_CRITICAL(&s_audio_state_lock);
    context->stop_pending = true;
    int result = 0;
    if (context->direction == AUDIO_DIRECTION_CAPTURE) {
        int64_t deadline = esp_timer_get_time() + AUDIO_STOP_TIMEOUT_MS * 1000;
        for (;;) {
            portENTER_CRITICAL(&s_audio_state_lock);
            unsigned active = context->in_flight;
            portEXIT_CRITICAL(&s_audio_state_lock);
            if (!active)
                break;
            if (esp_timer_get_time() >= deadline) {
                result = -1;
                goto done;
            }
            vTaskDelay(1);
        }
        if (disable_channel(s_audio.rx_channel, &s_audio.rx_enabled) < 0) {
            result = -1;
        }
        s_audio.capture_started = false;
#if MYBOT_LOCAL_AEC
        local_capture_release(context);
#endif
        if (result < 0)
            goto done;
    } else {
        if (set_pa(false) < 0 || mybot_pcm_playback_buffer_stop(context->playback_buffer) < 0) {
            result = -1;
            goto done;
        }
        /* TX auto-clears stale PCM while capture continues to own the clocks. */
        if (s_audio.tx_enabled)
            vTaskDelay(pdMS_TO_TICKS(120));
        s_audio.playback_started = false;
    }
    if (!s_audio.capture_started && !s_audio.playback_started &&
        (close_codec_locked() < 0 || disable_channel(s_audio.tx_channel, &s_audio.tx_enabled) < 0))
        result = -1;
done:
    context->stop_pending = result < 0;
    ESP_LOGI(TAG, "event=audio_device direction=%s action=stop result=%s",
             direction_name(context->direction), result ? "retry" : "ok");
    return result;
}
static int stream_stop(void *opaque) {
    audio_stream_context_t *context = opaque;
    if (!context || context->magic != AUDIO_CONTEXT_MAGIC || !audio_lock())
        return -1;
    int result = stream_stop_locked(context);
    audio_unlock();
    return result;
}

/* Called only after destroy() has transferred ownership to the driver. The SDK
 * joins its producer before destroy; playback_buffer_destroy separately joins
 * our FIFO worker. A failed stop/worker join retains every referenced object. */
static int destroy_owned_locked(audio_stream_context_t *context) {
    if (stream_stop_locked(context) < 0)
        return -1;
    if (context->playback_buffer) {
        if (mybot_pcm_playback_buffer_destroy(context->playback_buffer) < 0) {
            context->stop_pending = true;
            return -1;
        }
        context->playback_buffer = NULL;
    }
    if (s_audio.references == 1 && shared_release_locked() < 0)
        return -1;
    if (s_audio.references)
        --s_audio.references;
    context->magic = 0;
#if MYBOT_LOCAL_AEC
    local_capture_release(context);
#endif
    free(context->scratch);
    free(context);
    return 0;
}

static bool reap_deferred_locked(void) {
    /* Take one bounded pass. A concurrent destroy can enqueue another context,
     * and failures are retained for the next init/destroy attempt. */
    portENTER_CRITICAL(&s_audio_state_lock);
    audio_stream_context_t *pending = s_deferred;
    s_deferred = NULL;
    portEXIT_CRITICAL(&s_audio_state_lock);
    while (pending) {
        audio_stream_context_t *context = pending;
        pending = context->deferred_next;
        if (destroy_owned_locked(context) < 0) {
            portENTER_CRITICAL(&s_audio_state_lock);
            context->deferred_next = s_deferred;
            s_deferred = context;
            portEXIT_CRITICAL(&s_audio_state_lock);
            ESP_LOGW(TAG, "event=audio_device action=deferred_cleanup result=pending owner=driver");
        } else {
            ESP_LOGI(TAG, "event=audio_device action=deferred_cleanup result=ok");
        }
    }
    portENTER_CRITICAL(&s_audio_state_lock);
    bool complete = s_deferred == NULL;
    portEXIT_CRITICAL(&s_audio_state_lock);
    return complete;
}

static void stream_destroy(void *opaque) {
    audio_stream_context_t *context = opaque;
    if (!context || context->magic != AUDIO_CONTEXT_MAGIC)
        return;
    /* destroy is a void ownership transfer, not a retryable caller operation.
     * Publish ownership even if the lifecycle mutex or a hardware stop times
     * out; the next stream init retries cleanup before acquiring resources. */
    portENTER_CRITICAL(&s_audio_state_lock);
    if (!context->deferred) {
        context->deferred = true;
        context->deferred_next = s_deferred;
        s_deferred = context;
    }
    portEXIT_CRITICAL(&s_audio_state_lock);
    if (audio_lock()) {
        (void)reap_deferred_locked();
        audio_unlock();
    } else {
        ESP_LOGW(TAG, "event=audio_device action=destroy result=deferred owner=driver reason=lock");
    }
}

static int capture_reset(void *opaque) {
    audio_stream_context_t *context = opaque;
    if (!context_valid(context, AUDIO_DIRECTION_CAPTURE))
        return -1;
    /* The capture owner serializes reset with read. Admit the DSP operation
     * under the same stop fence as read, without taking the lifecycle mutex:
     * stop holds that mutex while it waits for in_flight to become zero. */
    portENTER_CRITICAL(&s_audio_state_lock);
    bool admitted = stream_running(context) && !context->in_flight;
    if (admitted)
        ++context->in_flight;
    portEXIT_CRITICAL(&s_audio_state_lock);
    if (!admitted)
        return -1;
    int result = 0;
#if MYBOT_LOCAL_AEC
    result = local_capture_reset(context);
#endif
    portENTER_CRITICAL(&s_audio_state_lock);
    bool running = stream_running(context);
    --context->in_flight;
    portEXIT_CRITICAL(&s_audio_state_lock);
    return running ? result : -1;
}

static int persist_volume(audio_volume_context_t *context, int volume) {
    if (nvs_set_i32(context->handle, AUDIO_VOLUME_NVS_KEY, volume) != ESP_OK ||
        nvs_commit(context->handle) != ESP_OK) {
        return -1;
    }
    context->persisted_volume = volume;
    context->persisted_volume_known = true;
    context->persist_dirty = false;
    return 0;
}

static void erase_invalid_volume(nvs_handle_t handle) {
    esp_err_t err = nvs_erase_key(handle, AUDIO_VOLUME_NVS_KEY);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "event=volume action=erase_invalid result=error reason=erase error=%s",
                 esp_err_to_name(err));
    } else if (err == ESP_OK && nvs_commit(handle) != ESP_OK) {
        ESP_LOGW(TAG, "event=volume action=erase_invalid result=error reason=commit");
    }
}

static int volume_init(void **out_context) {
    if (!out_context || s_volume_context.active) {
        return -1;
    }
    *out_context = NULL;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(AUDIO_VOLUME_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "event=volume action=restore result=error reason=nvs_open error=%s",
                 esp_err_to_name(err));
        return -1;
    }

    int volume = MYBOT_AUDIO_DEFAULT_VOLUME;
    const char *source = "default";
    int32_t stored_volume = 0;
    err = nvs_get_i32(handle, AUDIO_VOLUME_NVS_KEY, &stored_volume);
    bool stored_volume_valid = err == ESP_OK && stored_volume >= MYBOT_AUDIO_VOLUME_MIN &&
                               stored_volume <= MYBOT_AUDIO_VOLUME_MAX;
    if (stored_volume_valid) {
        volume = (int)stored_volume;
        source = "nvs";
    } else if (err == ESP_OK || err == ESP_ERR_NVS_TYPE_MISMATCH) {
        ESP_LOGW(TAG, "event=volume action=restore result=ignored reason=invalid_record");
        erase_invalid_volume(handle);
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "event=volume action=restore result=ignored reason=nvs_read error=%s",
                 esp_err_to_name(err));
    }

    s_volume_context = (audio_volume_context_t){
        .handle = handle,
        .active = true,
        .persisted_volume_known = stored_volume_valid,
        .persisted_volume = volume,
    };
    if (audio_lock()) {
        s_output_volume = volume;
        audio_unlock();
    }
    *out_context = &s_volume_context;
    ESP_LOGI(TAG, "event=volume action=restore value=%d source=%s result=ok", volume, source);
    return 0;
}

static int volume_set(void *opaque, int volume) {
    audio_volume_context_t *context = opaque;
    if (context != &s_volume_context || !context->active || volume < MYBOT_AUDIO_VOLUME_MIN ||
        volume > MYBOT_AUDIO_VOLUME_MAX || !audio_lock()) {
        return -1;
    }

    int previous_volume = s_output_volume;
    if (s_audio.playback_started &&
        esp_codec_dev_set_out_vol(s_audio.device, volume) != ESP_CODEC_DEV_OK) {
        audio_unlock();
        ESP_LOGE(TAG, "event=volume action=set value=%d result=error reason=codec", volume);
        return -1;
    }
    s_output_volume = volume;
    audio_unlock();

    if (previous_volume == volume && !context->persist_dirty && context->persisted_volume_known &&
        context->persisted_volume == volume) {
        return 0;
    }
    if (persist_volume(context, volume) < 0) {
        context->persist_dirty = true;
        ESP_LOGW(TAG,
                 "event=volume action=set value=%d applied=1 persisted=0 result=error "
                 "scope=persistence",
                 volume);
        return 0;
    }

    ESP_LOGI(TAG, "event=volume action=set value=%d persisted=1 result=ok", volume);
    return 0;
}

static int volume_get(void *opaque, int *volume) {
    audio_volume_context_t *context = opaque;
    if (context != &s_volume_context || !context->active || !volume || !audio_lock()) {
        return -1;
    }
    *volume = s_output_volume;
    audio_unlock();
    return 0;
}

static void volume_destroy(void *opaque) {
    audio_volume_context_t *context = opaque;
    if (context != &s_volume_context || !context->active) {
        return;
    }

    int volume = s_output_volume;
    bool persisted = context->persisted_volume_known && context->persisted_volume == volume;
    if (context->persist_dirty) {
        persisted = persist_volume(context, volume) == 0;
    }
    if (context->persist_dirty) {
        ESP_LOGW(TAG,
                 "event=volume action=detach value=%d persisted=0 result=error "
                 "scope=persistence",
                 volume);
    } else {
        ESP_LOGI(TAG, "event=volume action=detach value=%d persisted=%d result=ok", volume,
                 persisted ? 1 : 0);
    }
    nvs_close(context->handle);
    *context = (audio_volume_context_t){0};
}

static const mybot_audio_capture_ops_t s_capture_ops = {
    .init = capture_init,
    .start = capture_start,
    .read = capture_read,
    .stop = stream_stop,
    .destroy = stream_destroy,
    .reset = capture_reset,
};
static const mybot_audio_playback_ops_t s_playback_ops = {
    .init = playback_init,
    .start = playback_start,
    .write = playback_write,
    .stop = stream_stop,
    .destroy = stream_destroy,
};
static const mybot_audio_volume_ops_t s_volume_ops = {
    .init = volume_init,
    .set_volume = volume_set,
    .get_volume = volume_get,
    .destroy = volume_destroy,
};
const mybot_audio_capture_ops_t *mybot_codec_audio_capture_ops(void) {
    return &s_capture_ops;
}
const mybot_audio_playback_ops_t *mybot_codec_audio_playback_ops(void) {
    return &s_playback_ops;
}
const mybot_audio_volume_ops_t *mybot_codec_audio_volume_ops(void) {
    return &s_volume_ops;
}
