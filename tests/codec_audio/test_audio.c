/* SPDX-License-Identifier: Apache-2.0 */
#include "host_runtime.h"
#include "esp_aec.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#define free host_free
#include "../../components/mybot_platform/src/drivers/audio/codec_audio.c"
#undef TAG
#undef AUDIO_BUS_SLOTS
#include "../../components/mybot_platform/src/drivers/audio/mosaico_codec_audio.c"
#define TEST_BUS_SLOTS 2
#define TEST_START_STEPS 5
#define TEST_MIC_GAIN 24.0f
#undef free

static int fail_at, steps, channels, controls, codecs, devices, buffers, pa, opens;
static bool tx, rx, fail_stop, fail_close, fail_delete;
static bool fail_aec_create, fail_nlp;
static int aecs, aec_calls, aec_creates, aec_destroys;
static atomic_bool hold_read, read_entered;
static atomic_bool hold_aec_create, aec_create_entered;
static size_t capture_limit;
static int16_t capture_sample = 123, reference_sample = 999;
static bool capture_ramp;
static unsigned capture_sequence;
static const audio_codec_ctrl_if_t mock_control = {0};
static const audio_codec_if_t mock_codec = {0};
static int mock_device, mock_buffer;
static int32_t stored_volume = 42;
static bool step(void) {
    return ++steps == fail_at;
}
i2c_master_bus_handle_t mosaico_hardware_i2c_bus(void) {
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
esp_err_t i2s_channel_init_std_mode(i2s_chan_handle_t channel, const i2s_std_config_t *bus) {
    assert(channel && bus->clk_cfg.sample_rate_hz == 16000 && bus->clk_cfg.mclk_multiple == 256);
    assert(bus->slot_cfg.data_bit_width == 16 && bus->slot_cfg.slot_mode == 2);
    assert(bus->gpio_cfg.mclk == 54 && bus->gpio_cfg.dout == 52 && bus->gpio_cfg.din == 40);
    assert(bus->gpio_cfg.bclk == 37 && bus->gpio_cfg.ws == 49);
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
    uint32_t expected_timeout =
        MYBOT_LOCAL_AEC ? 50 + (uint32_t)(size / (TEST_BUS_SLOTS * 2) * 1000 / 16000) : 50;
    assert(channel == (void *)2 && rx && tx && timeout == expected_timeout &&
           size % (TEST_BUS_SLOTS * 2) == 0);
    atomic_store(&read_entered, true);
    while (atomic_load(&hold_read))
        host_real_delay(1);
    size_t frames = size / (TEST_BUS_SLOTS * sizeof(int16_t));
    if (capture_limit && frames > capture_limit)
        frames = capture_limit;
    int16_t *pcm = destination;
    for (size_t i = 0; i < frames; ++i) {
        int16_t sample = capture_ramp ? (int16_t)capture_sequence++ : capture_sample;
        pcm[i * TEST_BUS_SLOTS] = sample;
        pcm[i * TEST_BUS_SLOTS + 1] = capture_ramp ? (int16_t)(sample ^ 0x55) : reference_sample;
    }
    *bytes = frames * TEST_BUS_SLOTS * sizeof(int16_t);
    return *bytes == size ? ESP_OK : ESP_ERR_TIMEOUT;
}
esp_err_t i2s_channel_write(i2s_chan_handle_t channel, const void *source, size_t size,
                            size_t *bytes, uint32_t timeout) {
    assert(channel == (void *)1 && tx && timeout == 50 && size % (TEST_BUS_SLOTS * 2) == 0);
    const int16_t *pcm = source;
    for (size_t i = 0; i < size / sizeof(int16_t); ++i)
        assert(pcm[i] == 123);
    *bytes = size;
    return ESP_ERR_TIMEOUT; /* A timeout may still have completed full frames. */
}
const audio_codec_ctrl_if_t *audio_codec_new_i2c_ctrl(audio_codec_i2c_cfg_t *config) {
    assert(config->port == 0 && config->addr == 0x32 && config->bus_handle);
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
const audio_codec_if_t *es8311_codec_new(es8311_codec_cfg_t *config) {
    assert(config->codec_mode == ESP_CODEC_DEV_WORK_MODE_BOTH && config->use_mclk);
    assert(config->pa_pin == GPIO_NUM_NC && config->no_dac_ref == !MYBOT_LOCAL_AEC &&
           config->mclk_div == 256);
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
    assert(format->channel == TEST_BUS_SLOTS && opens == 0);
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
int esp_codec_dev_set_in_gain(esp_codec_dev_handle_t device, float gain) {
    assert(device == &mock_device && gain == TEST_MIC_GAIN);
    return step() ? ESP_FAIL : ESP_OK;
}
int esp_codec_dev_set_out_vol(esp_codec_dev_handle_t device, int volume) {
    assert(device == &mock_device && volume >= 0 && volume <= 100);
    return step() ? ESP_FAIL : ESP_OK;
}
int esp_codec_dev_read_reg(esp_codec_dev_handle_t device, int reg, int *value) {
    assert(device == &mock_device && reg == 0x44 && value && opens == 1);
    *value = 0x58;
    return step() ? ESP_CODEC_DEV_READ_FAIL : ESP_CODEC_DEV_OK;
}
static aec_handle_t mock_aec = {.frame_size = 64};
aec_handle_t *aec_create_from_config(aec_config_t *config) {
    assert(config && config->mic_num == 1 && config->ref_num == 1 && config->out_num == 1);
    assert(config->sample_rate == 16000 && config->filter_length == 4);
    assert(config->mode == AEC_MODE_FD_LOW_COST && config->nlp_level == AEC_NLP_LEVEL_AGGR);
    assert(config->caps & MALLOC_CAP_SPIRAM);
    atomic_store(&aec_create_entered, true);
    while (atomic_load(&hold_aec_create))
        host_real_delay(1);
    if (fail_aec_create)
        return NULL;
    assert(aecs == 0);
    ++aecs;
    ++aec_creates;
    return &mock_aec;
}
aec_nlp_level_t aec_set_nlp_level(const aec_handle_t *handle, aec_nlp_level_t level) {
    assert(handle == &mock_aec && level == AEC_NLP_LEVEL_AGGR);
    return fail_nlp ? AEC_NLP_LEVEL_NORMAL : level;
}
int aec_get_chunksize(const aec_handle_t *handle) {
    assert(handle == &mock_aec);
    return handle->frame_size;
}
char *aec_get_mode_string(aec_mode_t mode) {
    assert(mode == AEC_MODE_FD_LOW_COST);
    return "FD_LOW_COST";
}
char *aec_get_nlp_string(aec_nlp_level_t level) {
    assert(level == AEC_NLP_LEVEL_AGGR);
    return "AGGR";
}
void aec_process(const aec_handle_t *handle, int16_t *indata, int16_t *refdata, int16_t *outdata) {
    assert(handle == &mock_aec && indata && refdata && outdata);
    assert(!((uintptr_t)indata % 16) && !((uintptr_t)refdata % 16) && !((uintptr_t)outdata % 16));
    host_expect_psram(indata);
    host_expect_psram(refdata);
    host_expect_psram(outdata);
    ++aec_calls;
    for (int i = 0; i < handle->frame_size; ++i) {
        assert(refdata[i] == (capture_ramp ? (int16_t)(indata[i] ^ 0x55) : reference_sample));
        outdata[i] = indata[i];
    }
}
void aec_destroy(aec_handle_t *handle) {
    assert(handle == &mock_aec && aecs == 1);
    --aecs;
    ++aec_destroys;
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
    fail_stop = fail_close = fail_delete = false;
    fail_aec_create = fail_nlp = false;
    aecs = aec_calls = aec_creates = aec_destroys = 0;
    mock_aec.frame_size = 64;
    atomic_store(&hold_read, false);
    atomic_store(&read_entered, false);
    atomic_store(&hold_aec_create, false);
    atomic_store(&aec_create_entered, false);
    capture_limit = 0;
    capture_sample = 123;
    reference_sample = 999;
    capture_ramp = false;
    capture_sequence = 0;
}
static void *read_once(void *context) {
    int16_t output[2];
    assert(capture_read(context, output, 2) == 2);
    assert(output[0] == 123 && output[1] == 123);
    return NULL;
}

#if MYBOT_LOCAL_AEC
static void test_local_aec_creation_failures(void) {
    for (int allocation = 1; allocation <= 5; ++allocation) {
        reset();
        host_fail_alloc = allocation;
        assert(!mosaico_local_aec_create());
        assert(!host_allocations && !aecs);
    }
    const int invalid_chunks[] = {0, -1, 961};
    for (unsigned i = 0; i < sizeof(invalid_chunks) / sizeof(invalid_chunks[0]); ++i) {
        reset();
        mock_aec.frame_size = invalid_chunks[i];
        assert(!mosaico_local_aec_create());
        assert(!host_allocations && !aecs);
    }
    reset();
}

static void test_local_reset_discards_output_and_partial_input(void) {
    for (unsigned partial = 0; partial < 2; ++partial) {
        reset();
        mock_aec.frame_size = 256;
        void *capture = NULL;
        assert(capture_init(&capture, 16000, 1, 16) == 0 && capture_start(capture) == 0);
        audio_stream_context_t *context = capture;
        int16_t output[AUDIO_MAX_FRAMES];
        assert(capture_read(capture, output, AUDIO_MAX_FRAMES) == AUDIO_MAX_FRAMES);
        if (partial) {
            capture_limit = 160;
            assert(capture_read(capture, output, AUDIO_MAX_FRAMES) == 64);
            assert(context->local_aec_input_frames == 160);
        }
        int creates_before = aec_creates, destroys_before = aec_destroys;
        assert(s_capture_ops.reset(capture) == 0);
        assert(aec_creates == creates_before + 1 && aec_destroys == destroys_before + 1);
        assert(!context->local_aec_input_frames);
        assert(!mosaico_local_aec_read(context->local_aec, output, AUDIO_MAX_FRAMES));
        capture_limit = 0;
        capture_sample = 321;
        reference_sample = 654;
        assert(capture_read(capture, output, AUDIO_MAX_FRAMES) == AUDIO_MAX_FRAMES);
        for (unsigned i = 0; i < AUDIO_MAX_FRAMES; ++i)
            assert(output[i] == 321);

        fail_aec_create = true;
        assert(s_capture_ops.reset(capture) < 0);
        assert(!aecs && !mosaico_local_aec_read(context->local_aec, output, AUDIO_MAX_FRAMES));
        fail_aec_create = false;
        assert(s_capture_ops.reset(capture) == 0);
        assert(capture_read(capture, output, 1) == 1 && output[0] == 321);
        fail_nlp = true;
        assert(s_capture_ops.reset(capture) < 0);
        assert(!aecs && !mosaico_local_aec_read(context->local_aec, output, AUDIO_MAX_FRAMES));
        fail_nlp = false;
        assert(s_capture_ops.reset(capture) == 0);
        assert(stream_stop(capture) == 0);
        assert(s_capture_ops.reset(capture) < 0);
        stream_destroy(capture);
    }
    reset();
}

static void test_local_capture_continuity(void) {
    const int chunks[] = {64, 160, 256, 512, 960};
    const int requests[] = {1, 2, 63, 64, 65, 77, 959, 960};
    for (unsigned chunk = 0; chunk < sizeof(chunks) / sizeof(chunks[0]); ++chunk) {
        for (unsigned partial = 0; partial < 2; ++partial) {
            reset();
            mock_aec.frame_size = chunks[chunk];
            capture_ramp = true;
            capture_limit = partial ? 31 : 0;
            void *capture = NULL;
            assert(capture_init(&capture, 16000, 1, 16) == 0 && capture_start(capture) == 0);
            unsigned received = 0, reads = 0;
            while (received < 70000) {
                int16_t output[AUDIO_MAX_FRAMES];
                int requested = requests[reads++ % (sizeof(requests) / sizeof(requests[0]))];
                int count = capture_read(capture, output, requested);
                assert(count >= 0 && count <= requested && reads < 100000);
                for (int i = 0; i < count; ++i)
                    assert(output[i] == (int16_t)received++);
            }
            assert(stream_stop(capture) == 0);
            stream_destroy(capture);
            assert(!aecs && !host_allocations);
        }
    }
    reset();
}

typedef struct {
    void *capture;
    int result;
} reset_call_t;

static void *reset_once(void *opaque) {
    reset_call_t *call = opaque;
    call->result = s_capture_ops.reset(call->capture);
    return NULL;
}

static void test_stop_retains_in_flight_reset(void) {
    reset();
    void *capture = NULL;
    assert(capture_init(&capture, 16000, 1, 16) == 0 && capture_start(capture) == 0);
    atomic_store(&aec_create_entered, false);
    atomic_store(&hold_aec_create, true);
    reset_call_t call = {.capture = capture};
    pthread_t owner;
    assert(pthread_create(&owner, NULL, reset_once, &call) == 0);
    while (!atomic_load(&aec_create_entered))
        host_real_delay(1);
    assert(stream_stop(capture) < 0 && rx && tx && opens == 1);
    assert(host_allocations == 7); /* Context and DSP buffers stay owned until the reset exits. */
    atomic_store(&hold_aec_create, false);
    assert(pthread_join(owner, NULL) == 0 && call.result < 0);
    assert(stream_stop(capture) == 0 && !aecs);
    stream_destroy(capture);
    reset();
}
#endif

int main(void) {
#if MYBOT_LOCAL_AEC
    test_local_aec_creation_failures();
    test_local_reset_discards_output_and_partial_input();
    test_local_capture_continuity();
    test_stop_retains_in_flight_reset();
#endif
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
    for (int failure = 1; failure <= TEST_START_STEPS; ++failure) {
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
#if MYBOT_LOCAL_AEC
    {
        void *capture = NULL;
        assert(capture_init(&capture, 16000, 1, 16) == 0);
        fail_aec_create = true;
        assert(capture_start(capture) < 0 && !((audio_stream_context_t *)capture)->local_aec);
        fail_aec_create = false;
        assert(capture_start(capture) == 0 && ((audio_stream_context_t *)capture)->local_aec);
        assert(s_capture_ops.reset(capture) == 0);
        assert(read_once(capture) == NULL);
        int16_t output[AUDIO_MAX_FRAMES];
        assert(capture_read(capture, output, AUDIO_MAX_FRAMES) == AUDIO_MAX_FRAMES);
        for (int i = 0; i < AUDIO_MAX_FRAMES; ++i)
            assert(output[i] == 123);
        assert(aec_calls >= 15 && aec_creates == 2 && aec_destroys == 1);
        fail_aec_create = true;
        assert(s_capture_ops.reset(capture) < 0);
        assert(capture_read(capture, output, 2) < 0);
        fail_aec_create = false;
        assert(s_capture_ops.reset(capture) == 0);
        assert(read_once(capture) == NULL);
        assert(stream_stop(capture) == 0);
        stream_destroy(capture);
        reset();
    }
#endif
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
    assert(s_capture_ops.reset(capture) < 0); /* An in-flight read excludes reset. */
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
#if MYBOT_LOCAL_AEC
    assert(s_deferred && rx && tx && host_allocations == 7);
#else
    assert(s_deferred && rx && tx && host_allocations == 2);
#endif
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
    assert(capture_start(capture) == 0);
    fail_close = true;
    assert(stream_stop(capture) < 0 && opens == 1);
    assert(stream_stop(capture) == 0);
    stream_destroy(capture);
    reset();
    puts("Mosaico ES8311 lifecycle fault injection and PCM conversion passed");
}
