/* SPDX-License-Identifier: Apache-2.0 */
#include "host_runtime.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#define free host_free
#include "../../components/mybot_platform/src/drivers/audio/korvo_s31_codec_audio.c"
#undef free

static int fail_at, steps, channels, controls, codecs, devices, buffers, pa, opens;
static bool tx, rx, fail_stop, fail_close, fail_delete, bad_tdm;
static atomic_bool hold_read, read_entered;
static const audio_codec_ctrl_if_t mock_control = {0};
static const audio_codec_if_t mock_codec = {0};
static int mock_device, mock_buffer;
static int32_t stored_volume = 42;
static bool step(void) {
    return ++steps == fail_at;
}
i2c_master_bus_handle_t korvo_s31_hardware_i2c_bus(void) {
    return &mock_device;
}
esp_err_t gpio_set_level(gpio_num_t pin, unsigned level) {
    assert(pin == MYBOT_AUDIO_PA_GPIO);
    pa = (int)level;
    return ESP_OK;
}
esp_err_t i2s_new_channel(const i2s_chan_config_t *config, i2s_chan_handle_t *out,
                          i2s_chan_handle_t *in) {
    assert(config->dma_desc_num == 6 && config->dma_frame_num == 240);
    if (step())
        return ESP_FAIL;
    *out = (void *)1;
    *in = (void *)2;
    channels += 2;
    return ESP_OK;
}
esp_err_t i2s_channel_init_tdm_mode(i2s_chan_handle_t channel, const i2s_tdm_config_t *bus) {
    assert(channel && bus->clk_cfg.sample_rate_hz == 16000);
    assert(bus->slot_cfg.total_slot == 4 && bus->slot_cfg.ws_width == 32);
    assert(bus->slot_cfg.data_bit_width == 16 && bus->slot_cfg.slot_mask == 15);
    assert(bus->gpio_cfg.mclk == GPIO_NUM_NC && bus->gpio_cfg.dout == 5 && bus->gpio_cfg.din == 6);
    return step() ? ESP_FAIL : ESP_OK;
}
esp_err_t i2s_channel_enable(i2s_chan_handle_t channel) {
    if (step())
        return ESP_FAIL;
    if (channel == (void *)1) {
        assert(!tx);
        tx = true;
    } else {
        assert(!rx);
        rx = true;
    }
    return ESP_OK;
}
esp_err_t i2s_channel_disable(i2s_chan_handle_t channel) {
    if (channel == (void *)1) {
        assert(tx);
        tx = false;
    } else {
        assert(rx);
        rx = false;
    }
    return ESP_OK;
}
esp_err_t i2s_del_channel(i2s_chan_handle_t channel) {
    if (fail_delete) {
        fail_delete = false;
        return ESP_FAIL;
    }
    assert(channel && channels > 0);
    --channels;
    return ESP_OK;
}
esp_err_t i2s_channel_read(i2s_chan_handle_t channel, void *destination, size_t size, size_t *bytes,
                           uint32_t timeout) {
    assert(channel == (void *)2 && rx && tx && timeout == 50 && size % 8 == 0);
    atomic_store(&read_entered, true);
    while (atomic_load(&hold_read))
        host_real_delay(1);
    int16_t *pcm = destination;
    for (size_t i = 0; i < size / sizeof(int16_t); ++i)
        pcm[i] = i % 4 == 0 ? 123 : 999;
    *bytes = size;
    return ESP_OK;
}
esp_err_t i2s_channel_write(i2s_chan_handle_t channel, const void *source, size_t size,
                            size_t *bytes, uint32_t timeout) {
    assert(channel == (void *)1 && tx && timeout == 50 && size % 8 == 0);
    const int16_t *pcm = source;
    for (size_t i = 0; i < size / sizeof(int16_t); ++i)
        assert(pcm[i] == 123);
    *bytes = size;
    return ESP_ERR_TIMEOUT; /* A timeout may still have completed full frames. */
}
const audio_codec_ctrl_if_t *audio_codec_new_i2c_ctrl(audio_codec_i2c_cfg_t *config) {
    assert(config->port == 0 && config->addr == 0x20 && config->bus_handle);
    if (step())
        return NULL;
    ++controls;
    return &mock_control;
}
int audio_codec_delete_ctrl_if(const audio_codec_ctrl_if_t *iface) {
    assert(iface == &mock_control && controls == 1);
    --controls;
    return ESP_OK;
}
const audio_codec_if_t *es8389_codec_new(es8389_codec_cfg_t *config) {
    assert(config->codec_mode == ESP_CODEC_DEV_WORK_MODE_BOTH && !config->use_mclk);
    assert(config->pa_pin == GPIO_NUM_NC && !config->no_dac_ref);
    if (step())
        return NULL;
    ++codecs;
    return &mock_codec;
}
int audio_codec_delete_codec_if(const audio_codec_if_t *iface) {
    assert(iface == &mock_codec && codecs == 1);
    --codecs;
    return ESP_OK;
}
esp_codec_dev_handle_t esp_codec_dev_new(esp_codec_dev_cfg_t *config) {
    assert(config->dev_type == ESP_CODEC_DEV_TYPE_IN_OUT && config->codec_if == &mock_codec);
    if (step())
        return NULL;
    ++devices;
    return &mock_device;
}
void esp_codec_dev_delete(esp_codec_dev_handle_t device) {
    assert(device == &mock_device && devices == 1 && opens == 0);
    --devices;
}
int esp_codec_dev_open(esp_codec_dev_handle_t device, esp_codec_dev_sample_info_t *format) {
    assert(device == &mock_device && format->sample_rate == 16000 && format->bits_per_sample == 16);
    assert(format->channel == 4 && opens == 0);
    ++opens;
    return step() ? ESP_FAIL : ESP_OK;
}
int esp_codec_dev_close(esp_codec_dev_handle_t device) {
    assert(device == &mock_device && opens == 1);
    if (fail_close) {
        fail_close = false;
        return ESP_FAIL;
    }
    --opens;
    return ESP_OK;
}
int esp_codec_dev_read_reg(esp_codec_dev_handle_t device, int reg, int *value) {
    assert(device == &mock_device);
    switch (reg) {
    case ES8389_ADC_MODE_REG:
        *value = bad_tdm ? 0 : ES8389_SERIAL_TDM_ENABLE;
        break;
    case ES8389_CLOCK_SOURCE_REG:
        *value = 0x40;
        break;
    case ES8389_ADC_FORMAT_REG:
    case ES8389_DAC_FORMAT_REG:
        *value = ES8389_FORMAT_PHILIPS_S16;
        break;
    case ES8389_ADC_SLOT_REG:
        *value = 0;
        break;
    case ES8389_DAC_SLOT_REG:
        *value = 0x0F;
        break;
    default:
        assert(!"unexpected codec register");
    }
    return step() ? ESP_FAIL : ESP_OK;
}
int esp_codec_dev_set_in_gain(esp_codec_dev_handle_t device, float gain) {
    assert(device == &mock_device && gain == 30.0f);
    return step() ? ESP_FAIL : ESP_OK;
}
int esp_codec_dev_set_out_vol(esp_codec_dev_handle_t device, int volume) {
    assert(device == &mock_device && volume >= 0 && volume <= 100);
    return step() ? ESP_FAIL : ESP_OK;
}
mybot_pcm_playback_buffer_t *mybot_pcm_playback_buffer_create(mybot_pcm_write_fn write, void *ctx) {
    assert(write == playback_write_i2s && ctx);
    if (step())
        return NULL;
    ++buffers;
    return (void *)&mock_buffer;
}
int mybot_pcm_playback_buffer_destroy(mybot_pcm_playback_buffer_t *buffer) {
    assert(buffer == (void *)&mock_buffer && buffers == 1);
    --buffers;
    return 0;
}
int mybot_pcm_playback_buffer_start(mybot_pcm_playback_buffer_t *buffer) {
    assert(buffer == (void *)&mock_buffer);
    return step() ? -1 : 0;
}
int mybot_pcm_playback_buffer_stop(mybot_pcm_playback_buffer_t *buffer) {
    assert(buffer == (void *)&mock_buffer);
    return fail_stop ? -1 : 0;
}
int mybot_pcm_playback_buffer_write(mybot_pcm_playback_buffer_t *buffer, const void *pcm,
                                    int frames) {
    assert(buffer == (void *)&mock_buffer && pcm);
    return frames;
}
int mybot_pcm_playback_buffer_drain(mybot_pcm_playback_buffer_t *buffer, uint32_t timeout) {
    assert(buffer == (void *)&mock_buffer && timeout == 1000);
    return 0;
}
esp_err_t nvs_open(const char *space, int mode, nvs_handle_t *handle) {
    assert(!strcmp(space, "audio") && mode == NVS_READWRITE);
    *handle = 1;
    return ESP_OK;
}
esp_err_t nvs_get_i32(nvs_handle_t handle, const char *key, int32_t *value) {
    assert(handle == 1 && !strcmp(key, "output_volume"));
    *value = stored_volume;
    return ESP_OK;
}
esp_err_t nvs_set_i32(nvs_handle_t handle, const char *key, int32_t value) {
    assert(handle == 1 && !strcmp(key, "output_volume"));
    stored_volume = value;
    return ESP_OK;
}
esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key) {
    (void)handle;
    (void)key;
    return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t handle) {
    assert(handle == 1);
    return ESP_OK;
}
void nvs_close(nvs_handle_t handle) {
    assert(handle == 1);
}

static void reset(void) {
    assert(channels == 0 && controls == 0 && codecs == 0 && devices == 0 && buffers == 0);
    assert(!tx && !rx && opens == 0 && pa == 0 && s_audio.references == 0 && !s_deferred);
    if (s_audio_mutex) {
        vSemaphoreDelete(s_audio_mutex);
        s_audio_mutex = NULL;
    }
    host_reset();
    steps = fail_at = 0;
    fail_stop = fail_close = fail_delete = bad_tdm = false;
}
static void *read_once(void *context) {
    int16_t output[2];
    assert(capture_read(context, output, 2) == 2);
    assert(output[0] == 123 && output[1] == 123);
    return NULL;
}
int main(void) {
    for (int direction = 0; direction < 2; ++direction) {
        for (int fail = 1; fail <= (direction ? 7 : 6); ++fail) {
            fail_at = fail;
            void *context = NULL;
            assert(stream_init(&context, 16000, 1, 16, (audio_direction_t)direction) < 0 &&
                   !context);
            reset();
        }
    }
    for (int allocation = 1; allocation <= 2; ++allocation) {
        host_fail_alloc = allocation;
        void *context = NULL;
        assert(capture_init(&context, 16000, 1, 16) < 0);
        reset();
    }
    for (int failure = 1; failure <= 11; ++failure) {
        void *capture = NULL;
        assert(capture_init(&capture, 16000, 1, 16) == 0);
        host_expect_psram(capture);
        host_expect_psram(((audio_stream_context_t *)capture)->scratch);
        fail_at = steps + failure;
        assert(capture_start(capture) < 0);
        fail_at = 0;
        assert(capture_start(capture) == 0);
        assert(stream_stop(capture) == 0);
        stream_destroy(capture);
        reset();
    }
    void *capture = NULL, *playback = NULL, *volume = NULL;
    assert(capture_init(&capture, 16000, 1, 16) == 0 &&
           playback_init(&playback, 16000, 1, 16) == 0);
    assert(volume_init(&volume) == 0 && s_output_volume == 42);
    for (int repeat = 0; repeat < 3; ++repeat) {
        assert(capture_start(capture) == 0 && playback_start(playback) == 0 && pa == 1 &&
               opens == 1);
        int16_t pcm[2] = {123, 123};
        size_t written = 0;
        assert(playback_write_i2s(playback, pcm, 2, &written, 50) == ESP_ERR_TIMEOUT &&
               written == 2);
        assert(mybot_audio_playback_drain(playback, 1000) == 0);
        assert(volume_set(volume, 63) == 0 && stored_volume == 63);
        assert(stream_stop(capture) == 0 && tx && pa && opens == 1);
        assert(stream_stop(playback) == 0 && !tx && !rx && !pa && opens == 0);
        assert(playback_start(playback) == 0 && capture_start(capture) == 0);
        fail_stop = true;
        assert(stream_stop(playback) < 0 && tx && rx && opens == 1);
        fail_stop = false;
        assert(stream_stop(playback) == 0 && tx && rx && pa == 0);
        assert(stream_stop(capture) == 0);
    }
    assert(capture_start(capture) == 0);
    atomic_store(&hold_read, true);
    atomic_store(&read_entered, false);
    pthread_t reader;
    assert(pthread_create(&reader, NULL, read_once, capture) == 0);
    while (!atomic_load(&read_entered))
        host_real_delay(1);
    assert(stream_stop(capture) < 0 && rx && tx && opens == 1);
    atomic_store(&hold_read, false);
    assert(pthread_join(reader, NULL) == 0);
    assert(stream_stop(capture) == 0);
    volume_destroy(volume);
    stream_destroy(capture);
    fail_delete = true;
    stream_destroy(playback);
    playback = NULL; /* Public void destroy transfers ownership permanently. */
    assert(s_audio.references == 1 && !s_audio.ready && channels == 2 && s_deferred);
    void *other = NULL;
    fail_delete = true; /* The next init retries; a persistent fault still retains ownership. */
    assert(capture_init(&other, 16000, 1, 16) < 0 && !other && s_deferred);
    assert(capture_init(&other, 16000, 1, 16) == 0 && other && !s_deferred);
    stream_destroy(other);
    reset();

    assert(playback_init(&playback, 16000, 1, 16) == 0 && playback_start(playback) == 0);
    fail_stop = true;
    stream_destroy(playback);
    playback = NULL;
    assert(s_deferred && buffers == 1 && tx && opens == 1);
    assert(capture_init(&other, 16000, 1, 16) < 0 && !other && buffers == 1);
    fail_stop = false;
    assert(capture_init(&other, 16000, 1, 16) == 0 && !s_deferred && buffers == 0);
    stream_destroy(other);
    reset();

    assert(capture_init(&capture, 16000, 1, 16) == 0 && capture_start(capture) == 0);
    atomic_store(&hold_read, true);
    atomic_store(&read_entered, false);
    assert(pthread_create(&reader, NULL, read_once, capture) == 0);
    while (!atomic_load(&read_entered))
        host_real_delay(1);
    stream_destroy(capture);
    capture = NULL;
    assert(s_deferred && rx && tx && host_allocations == 2);
    assert(capture_init(&other, 16000, 1, 16) < 0 && !other && s_deferred);
    atomic_store(&hold_read, false);
    assert(pthread_join(reader, NULL) == 0);
    assert(capture_init(&other, 16000, 1, 16) == 0 && !s_deferred);
    stream_destroy(other);
    reset();

    assert(playback_init(&playback, 16000, 1, 16) == 0);
    assert(xSemaphoreTake(s_audio_mutex, 0) == pdTRUE);
    stream_destroy(playback); /* Lock timeout must not lose the ownership transfer. */
    playback = NULL;
    assert(s_deferred && buffers == 1);
    xSemaphoreGive(s_audio_mutex);
    assert(capture_init(&other, 16000, 1, 16) == 0 && !s_deferred && buffers == 0);
    stream_destroy(other);
    reset();
    assert(capture_init(&capture, 16000, 1, 16) == 0);
    bad_tdm = true;
    assert(capture_start(capture) < 0);
    bad_tdm = false;
    assert(capture_start(capture) == 0);
    fail_close = true;
    assert(stream_stop(capture) < 0 && opens == 1);
    assert(stream_stop(capture) == 0);
    stream_destroy(capture);
    reset();
    puts("S31 audio lifecycle fault injection and PCM conversion passed");
}
