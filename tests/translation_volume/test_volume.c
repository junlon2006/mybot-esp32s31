/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Exercise the real key handler and refresh, with production media/audio/presenter code. */
#include "../../components/mybot_stack/mybot_sdk/mybot/src/core/mybot_app.c"

static mybot_runtime_t runtime;
static mybot_platform_descriptor_t platform;
static mybot_lcd_content_t displayed;
static int hardware_volume;
static bool fail_init, fail_get, fail_set;
static unsigned reads, sets, renders, tests;

void aosl_msleep(unsigned milliseconds) {
    (void)milliseconds;
    abort();
}

void *aosl_hal_malloc(size_t size) {
    (void)size;
    abort();
}
void aosl_hal_free(void *memory) {
    if (memory)
        abort();
}

void test_log(const char *format, ...) {
    (void)format;
}

static int volume_init(void **context) {
    if (fail_init)
        return -1;
    *context = &hardware_volume;
    return 0;
}

static int volume_get(void *context, int *volume) {
    assert(context == &hardware_volume);
    reads++;
    /* Even a failing implementation must not leak an unvalidated value to the UI. */
    *volume = hardware_volume;
    return fail_get ? -1 : 0;
}

static int volume_set(void *context, int volume) {
    assert(context == &hardware_volume);
    assert(volume >= 0 && volume <= 100);
    sets++;
    if (fail_set)
        return -1;
    hardware_volume = volume;
    return 0;
}

static void volume_destroy(void *context) {
    assert(context == &hardware_volume);
}

static const mybot_audio_volume_ops_t volume_ops = {
    .init = volume_init,
    .set_volume = volume_set,
    .get_volume = volume_get,
    .destroy = volume_destroy,
};

const mybot_platform_descriptor_t *mybot_platform_registry_get(void) {
    return &platform;
}

int mybot_lcd_show_content(mybot_lcd_t *lcd, const mybot_lcd_content_t *content) {
    assert(lcd == &runtime.presenter.lcd);
    displayed = *content;
    renders++;
    return 0;
}

/* Volume controls must never flush/restart session workers or touch their PCM. */
int aosl_mpq_call(aosl_mpq_t queue, aosl_ref_t reference, const char *name,
                  aosl_mpq_func_t callback, uintptr_t count, ...) {
    (void)queue;
    (void)reference;
    (void)name;
    (void)callback;
    (void)count;
    abort();
}
aosl_mpq_t aosl_mpq_create_flags(int flags, int priority, int stack, int maximum, const char *name,
                                 void *init, void *fini, void *context) {
    (void)flags;
    (void)priority;
    (void)stack;
    (void)maximum;
    (void)name;
    (void)init;
    (void)fini;
    (void)context;
    abort();
}
int aosl_mpq_destroy_wait(aosl_mpq_t queue) {
    (void)queue;
    abort();
}
aosl_timer_t aosl_mpq_set_timer(uintptr_t interval, aosl_timer_func_t callback, void *destructor,
                                uintptr_t argc, ...) {
    (void)interval;
    (void)callback;
    (void)destructor;
    (void)argc;
    abort();
}
int aosl_mpq_kill_timer(aosl_timer_t timer) {
    (void)timer;
    abort();
}
int mybot_ringbuf_write(mybot_ringbuf_t buffer, const char *data, int bytes) {
    (void)buffer;
    (void)data;
    (void)bytes;
    abort();
}
uint32_t mybot_announce_get_generation(mybot_announce_t *announce) {
    (void)announce;
    abort();
}
int mybot_ringbuf_get_data_size(mybot_ringbuf_t buffer) {
    (void)buffer;
    abort();
}
int mybot_ringbuf_read(char *data, int bytes, mybot_ringbuf_t buffer) {
    (void)data;
    (void)bytes;
    (void)buffer;
    abort();
}

/* USB pipeline creation is unreachable from volume-only controls. */
mybot_ringbuf_t mybot_ringbuf_create(int bytes) {
    (void)bytes;
    abort();
}
int mybot_ringbuf_destroy(mybot_ringbuf_t buffer) {
    (void)buffer;
    abort();
}
int mybot_ringbuf_get_free_size(mybot_ringbuf_t buffer) {
    (void)buffer;
    abort();
}
int mybot_announce_init(mybot_announce_t *announce) {
    (void)announce;
    abort();
}
void mybot_announce_deinit(mybot_announce_t *announce) {
    (void)announce;
    abort();
}
void mybot_announce_stop(mybot_announce_t *announce) {
    (void)announce;
    abort();
}
bool mybot_announce_is_active(mybot_announce_t *announce) {
    (void)announce;
    abort();
}
int mybot_announce_read_pcm(mybot_announce_t *announce, int16_t *pcm, int frames,
                            uint32_t *generation) {
    (void)announce;
    (void)pcm;
    (void)frames;
    (void)generation;
    abort();
}
int mybot_agora_rtc_send_audio(mybot_translation_direction_t direction, const void *data,
                               size_t len) {
    (void)direction;
    (void)data;
    (void)len;
    abort();
}
void mybot_device_lifecycle_notify_conversation_ended(mybot_device_lifecycle_t *lifecycle) {
    (void)lifecycle;
    abort();
}

/* Volume key dispatch must never reach a conversation or language operation. */
int mybot_agora_rtc_fini(void) {
    abort();
}
void mybot_device_lifecycle_request_start(mybot_device_lifecycle_t *lifecycle) {
    (void)lifecycle;
    abort();
}
void mybot_device_lifecycle_request_stop(mybot_device_lifecycle_t *lifecycle) {
    (void)lifecycle;
    abort();
}
void mybot_device_lifecycle_request_pair(mybot_device_lifecycle_t *lifecycle) {
    (void)lifecycle;
    abort();
}
int mybot_device_lifecycle_cycle_language(mybot_device_lifecycle_t *lifecycle, bool source) {
    (void)lifecycle;
    (void)source;
    abort();
}
int mybot_device_lifecycle_set_languages(mybot_device_lifecycle_t *lifecycle, const char *source,
                                         const char *target) {
    (void)lifecycle;
    (void)source;
    (void)target;
    abort();
}
void mybot_device_lifecycle_tick(mybot_device_lifecycle_t *lifecycle) {
    assert(lifecycle == &runtime.lifecycle);
}

static void reset(int initial_volume, bool use_hardware, bool initialization_fails) {
    memset(&runtime, 0, sizeof(runtime));
    memset(&platform, 0, sizeof(platform));
    memset(&displayed, 0, sizeof(displayed));
    hardware_volume = initial_volume;
    fail_init = initialization_fails;
    fail_get = fail_set = false;
    reads = sets = renders = 0;
    platform.audio_volume = use_hardware ? &volume_ops : NULL;
    mybot_media_pipeline_init(&runtime.media);
    mybot_audio_context_init(&runtime.media.audio);
    if (use_hardware)
        assert((mybot_audio_device_volume_init(&runtime.media.audio) < 0) == initialization_fails);
    aosl_atomic_set(&runtime.running, true);
    runtime.ui_generation = 7;
    aosl_atomic_set(&runtime.conversation_generation, 11);
    assert(mybot_state_model_begin_start(&runtime.state_model));
    assert(mybot_state_model_begin_services(&runtime.state_model));
    assert(mybot_state_model_services_ready(&runtime.state_model));
    assert(mybot_state_model_set_device_state(&runtime.state_model, MYBOT_DEVICE_STATE_RUNTIME));
    runtime.presenter.active = true;
    strcpy(runtime.lifecycle.source_language, "zh-CN");
    strcpy(runtime.lifecycle.target_language, "en-US");
}

static void refresh(void) {
    refresh_translation_view(&runtime);
    mybot_presenter_render_state(&runtime.presenter, &runtime.state_model);
}

static void expect_display(int volume) {
    assert(renders > 0);
    assert(displayed.volume == volume);
    assert(runtime.presenter.volume == volume);
    assert(displayed.screen == MYBOT_LCD_SCREEN_READY);
    assert(strcmp(displayed.source_language, "zh-CN") == 0);
    assert(strcmp(displayed.target_language, "en-US") == 0);
}

static void press(mybot_key_event_t event) {
    uintptr_t arguments[] = {(uintptr_t)&runtime, (uintptr_t)event, runtime.ui_generation,
                             (uintptr_t)aosl_atomic_read(&runtime.conversation_generation)};
    unsigned previous = renders;
    handle_key_event(NULL, NULL, 4, arguments);
    assert(renders == previous + 1);
}

static void test_initial_and_restored_volume(void) {
    const int initial_values[] = {60, 40};
    for (unsigned i = 0; i < sizeof(initial_values) / sizeof(initial_values[0]); ++i) {
        reset(initial_values[i], true, false);
        assert(mybot_audio_get_media_volume(&runtime.media.audio) == 100);
        refresh();
        expect_display(initial_values[i]);
        assert(sets == 0); /* Reading the UI must not overwrite a restored hardware value. */
    }
    tests++;
}

static void test_hardware_adjustment(void) {
    reset(60, true, false);
    refresh();
    press(MYBOT_KEY_EVENT_VOLUME_UP);
    assert(hardware_volume == 70 && sets == 1);
    expect_display(70);
    press(MYBOT_KEY_EVENT_VOLUME_DOWN);
    assert(hardware_volume == 60 && sets == 2);
    expect_display(60);
    assert(mybot_audio_get_media_volume(&runtime.media.audio) == 100);
    assert(mybot_media_pipeline_set_volume(&runtime.media, 35) == 0);
    refresh();
    expect_display(35);
    tests++;
}

static void test_saturation(void) {
    reset(95, true, false);
    press(MYBOT_KEY_EVENT_VOLUME_UP);
    expect_display(100);
    press(MYBOT_KEY_EVENT_VOLUME_UP);
    expect_display(100);
    assert(hardware_volume == 100);
    hardware_volume = 5;
    press(MYBOT_KEY_EVENT_VOLUME_DOWN);
    expect_display(0);
    press(MYBOT_KEY_EVENT_VOLUME_DOWN);
    expect_display(0);
    assert(hardware_volume == 0);
    tests++;
}

static void test_hardware_set_failure(void) {
    reset(40, true, false);
    fail_set = true;
    press(MYBOT_KEY_EVENT_VOLUME_UP);
    assert(hardware_volume == 40 && sets == 1);
    expect_display(40);
    assert(runtime.media.audio.device_volume == 40);
    assert(mybot_media_pipeline_set_volume(&runtime.media, 75) < 0);
    refresh();
    expect_display(40);
    tests++;
}

static void test_unavailable_or_invalid_read(void) {
    reset(60, true, false);
    fail_get = true;
    int output = -7;
    assert(mybot_media_pipeline_get_volume(&runtime.media, &output) < 0 && output == -7);
    press(MYBOT_KEY_EVENT_VOLUME_DOWN);
    expect_display(-1);
    assert(sets == 0 && hardware_volume == 60);
    fail_get = false;
    const int invalid_values[] = {-1, 101};
    for (unsigned i = 0; i < sizeof(invalid_values) / sizeof(invalid_values[0]); ++i) {
        hardware_volume = invalid_values[i];
        press(MYBOT_KEY_EVENT_VOLUME_UP);
        expect_display(-1);
        assert(sets == 0);
    }
    hardware_volume = 45;
    refresh();
    expect_display(45);
    tests++;
}

static void test_software_fallback(void) {
    for (unsigned initialization_fails = 0; initialization_fails < 2; ++initialization_fails) {
        reset(60, initialization_fails != 0, initialization_fails != 0);
        refresh();
        expect_display(100);
        press(MYBOT_KEY_EVENT_VOLUME_DOWN);
        expect_display(90);
        assert(sets == 0 && hardware_volume == 60);
        assert(mybot_media_pipeline_set_volume(&runtime.media, 5) == 0);
        press(MYBOT_KEY_EVENT_VOLUME_DOWN);
        expect_display(0);
        assert(mybot_media_pipeline_set_volume(&runtime.media, 95) == 0);
        press(MYBOT_KEY_EVENT_VOLUME_UP);
        expect_display(100);
    }
    tests++;
}

static void tick(void) {
    uintptr_t argument = (uintptr_t)&runtime;
    control_tick_timer(0, NULL, 1, &argument);
}

static void test_unknown_read_recovers(void) {
    reset(40, true, false);
    fail_get = true;
    refresh();
    expect_display(-1);
    unsigned previous_reads = reads;
    unsigned previous_renders = renders;
    for (unsigned i = 1; i < VOLUME_READ_RETRY_TICKS; ++i)
        tick();
    assert(reads == previous_reads && renders == previous_renders);
    tick();
    assert(reads == previous_reads + 1 && renders == previous_renders);
    fail_get = false;
    for (unsigned i = 0; i < VOLUME_READ_RETRY_TICKS; ++i)
        tick();
    expect_display(40);
    assert(reads == previous_reads + 2 && renders == previous_renders + 1);
    for (unsigned i = 0; i < VOLUME_READ_RETRY_TICKS * 2; ++i)
        tick();
    assert(reads == previous_reads + 2 && renders == previous_renders + 1);
    tests++;
}

static void test_invalid_arguments(void) {
    reset(40, true, false);
    int output = -7;
    assert(mybot_media_pipeline_get_volume(NULL, &output) < 0 && output == -7);
    assert(mybot_media_pipeline_get_volume(&runtime.media, NULL) < 0);
    assert(mybot_media_pipeline_set_volume(&runtime.media, -1) < 0);
    assert(mybot_media_pipeline_set_volume(&runtime.media, 101) < 0);
    assert(mybot_media_pipeline_adjust_volume(&runtime.media, -101) < 0);
    assert(mybot_media_pipeline_adjust_volume(&runtime.media, 101) < 0);
    refresh();
    expect_display(40);
    assert(sets == 0);
    tests++;
}

int main(void) {
    test_initial_and_restored_volume();
    test_hardware_adjustment();
    test_saturation();
    test_hardware_set_failure();
    test_unavailable_or_invalid_read();
    test_software_fallback();
    test_unknown_read_recovers();
    test_invalid_arguments();
    printf("translation volume: %u tests passed\n", tests);
    return 0;
}
