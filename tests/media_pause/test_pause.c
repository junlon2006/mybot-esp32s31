/* SPDX-License-Identifier: Apache-2.0 */
#include "mybot_media_pipeline.h"
#include "mybot_platform_registry.h"

#include <assert.h>
#include <limits.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    CAPTURE_QUEUE = 1,
    PLAYBACK_QUEUE,
    SEND_QUEUE,
    USB_CAPTURE_QUEUE,
    USB_PLAYBACK_QUEUE,
    USB_SEND_QUEUE,
    QUEUE_COUNT,
    CONTROL_QUEUE = QUEUE_COUNT,
    RTC_QUEUE,
};
enum { MAX_QUEUED = 32, MAX_DEVICE_FRAMES = MYBOT_MEDIA_FRAME_SAMPLES * 8 };

typedef struct {
    aosl_mpq_func_t callback;
    uintptr_t argc;
    uintptr_t argv[3];
    aosl_mpq_t queue;
} queued_call_t;

typedef struct {
    aosl_timer_func_t callback;
    uintptr_t argument;
    bool active;
    void (*fini)(void *);
    void *context;
} worker_t;

static mybot_media_pipeline_t pipeline;
static mybot_media_pipeline_t meeting_usb;
static mybot_platform_descriptor_t platform;
static worker_t workers[QUEUE_COUNT];
static queued_call_t queued[MAX_QUEUED];
static unsigned queued_count;
static aosl_mpq_t current_queue, next_queue, fail_queue, delay_queue;
static bool fail_pb_stop, fail_pb_start, stop_during_wait, release_producer_during_wait;
static bool fail_destroy;
static bool cap_active, pb_active, usb_active, usb_pb_active;
static bool usb_disconnected, pause_usb_write;
static bool physical_playback_fault, pause_physical_write, stop_physical_write;
static unsigned audio_errors, volume_inits, announce_inits;
static bool fail_usb_start, fail_cap_stop, fail_codec_start, fail_cap_create;
static unsigned usb_destroyed, codec_destroyed;
static int16_t captured_sample, played[MAX_DEVICE_FRAMES], sent[MYBOT_MEDIA_FRAME_SAMPLES * 2];
static int16_t usb_captured_sample, usb_sent[MYBOT_MEDIA_FRAME_SAMPLES * 2];
static unsigned usb_sends;
static unsigned played_frames, sends, capture_reads, playback_writes, pb_stops, cap_stops;
static unsigned hardware_volume_sets, waited_ms, tests;
static int capture_frames, write_limit, hardware_volume;
static bool tick_during_fence;
static unsigned fence_ticks;
static bool disconnect_during_capture, check_reconnect_owner;
static aosl_mpq_t fail_call_queue, reconnect_calls[6];
static unsigned reconnect_call_count;
static bool hold_remote_producer, producer_acquired, release_remote_producer;
static pthread_mutex_t producer_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t producer_cond = PTHREAD_COND_INITIALIZER;

void test_atomic_cmpxchg_hook(aosl_atomic_t *value, intptr_t previous, intptr_t next) {
    if (value != &pipeline.remote_audio_busy || previous != 0 || next != 1 ||
        !__atomic_exchange_n(&hold_remote_producer, false, __ATOMIC_SEQ_CST))
        return;
    assert(pthread_mutex_lock(&producer_lock) == 0);
    producer_acquired = true;
    assert(pthread_cond_signal(&producer_cond) == 0);
    while (!release_remote_producer)
        assert(pthread_cond_wait(&producer_cond, &producer_lock) == 0);
    assert(pthread_mutex_unlock(&producer_lock) == 0);
}

void test_log(const char *format, ...) {
    (void)format;
}

void *aosl_hal_malloc(size_t size) {
    return malloc(size);
}

void aosl_hal_free(void *memory) {
    free(memory);
}

const mybot_platform_descriptor_t *mybot_platform_registry_get(void) {
    return &platform;
}

static int capture_init(void **context, int rate, int channels, int bits) {
    assert(rate == 16000 && channels == 1 && bits == 16);
    *context = &cap_active;
    return 0;
}

static int capture_start(void *context) {
    assert(context == &cap_active);
    if (fail_codec_start)
        return -1;
    cap_active = true;
    return 0;
}

static int capture_read(void *context, void *buffer, int frames) {
    assert(context == &cap_active && cap_active);
    assert(frames == 960);
    capture_reads++;
    if (capture_frames > 0) {
        int16_t *samples = buffer;
        for (int i = 0; i < capture_frames; ++i)
            samples[i] = captured_sample;
    }
    if (disconnect_during_capture) {
        disconnect_during_capture = false;
        mybot_media_pipeline_set_rtc_connected(&pipeline, false);
        mybot_media_pipeline_set_rtc_connected(&meeting_usb, false);
    }
    return capture_frames;
}

static int capture_stop(void *context) {
    assert(context == &cap_active);
    cap_stops++;
    if (fail_cap_stop)
        return -1;
    cap_active = false;
    return 0;
}

static void capture_destroy(void *context) {
    assert(context == &cap_active && !cap_active);
    codec_destroyed++;
}

static int playback_init(void **context, int rate, int channels, int bits) {
    assert(rate == 16000 && channels == 1 && bits == 16);
    *context = &pb_active;
    return 0;
}

static int playback_start(void *context) {
    assert(context == &pb_active || context == &usb_pb_active);
    if (fail_pb_start)
        return -1;
    assert(cap_active || usb_active); /* Playback is independent of physical capture. */
    *(bool *)context = true;
    if (context == &pb_active)
        physical_playback_fault = false;
    return 0;
}

static int playback_write(void *context, const void *buffer, int frames) {
    assert(context == &pb_active || context == &usb_pb_active);
    playback_writes++;
    if (context == &pb_active &&
        (physical_playback_fault || pause_physical_write || stop_physical_write)) {
        if (pause_physical_write)
            aosl_atomic_set(&pipeline.paused, true);
        if (stop_physical_write)
            aosl_atomic_set(&pipeline.running, false);
        return -1;
    }
    if (context == &usb_pb_active && pause_usb_write) {
        aosl_atomic_set(&pipeline.paused, true);
        return -1;
    }
    if (!*(bool *)context)
        return 0;
    int written = frames < write_limit ? frames : write_limit;
    assert(written >= 0 && played_frames + (unsigned)written <= MAX_DEVICE_FRAMES);
    memcpy(played + played_frames, buffer, (size_t)written * sizeof(int16_t));
    played_frames += (unsigned)written;
    return written;
}

static int playback_stop(void *context) {
    assert(context == &pb_active || context == &usb_pb_active);
    pb_stops++;
    if (fail_pb_stop)
        return -1;
    *(bool *)context = false;
    played_frames = 0; /* Model the driver dropping its asynchronous PCM queue. */
    return 0;
}

static void playback_destroy(void *context) {
    assert((context == &pb_active || context == &usb_pb_active) && !*(bool *)context);
}

static int volume_init(void **context) {
    volume_inits++;
    *context = &hardware_volume;
    return 0;
}

static int volume_get(void *context, int *value) {
    assert(context == &hardware_volume);
    *value = hardware_volume;
    return 0;
}

static int volume_set(void *context, int value) {
    assert(context == &hardware_volume);
    hardware_volume_sets++;
    hardware_volume = value;
    return 0;
}

static void volume_destroy(void *context) {
    assert(context == &hardware_volume);
}

static const mybot_audio_capture_ops_t capture_ops = {capture_init, capture_start, capture_read,
                                                      capture_stop, capture_destroy};
static int usb_init(void **context, int rate, int channels, int bits) {
    assert(rate == 16000 && channels == 1 && bits == 16);
    *context = &usb_active;
    return 0;
}
static int usb_start(void *context) {
    assert(context == &usb_active);
    if (fail_usb_start)
        return -1;
    usb_active = true;
    return 0;
}
static int usb_read(void *context, void *buffer, int frames) {
    assert(context == &usb_active && usb_active);
    if (usb_disconnected)
        return -1;
    int16_t *samples = buffer;
    for (int i = 0; i < frames; ++i)
        samples[i] = usb_captured_sample;
    return frames;
}
static int usb_stop(void *context) {
    assert(context == &usb_active);
    usb_active = false;
    return 0;
}
static void usb_destroy(void *context) {
    assert(context == &usb_active && !usb_active);
    usb_destroyed++;
}
static const mybot_audio_capture_ops_t usb_ops = {usb_init, usb_start, usb_read, usb_stop,
                                                  usb_destroy};
static const mybot_audio_playback_ops_t playback_ops = {
    playback_init, playback_start, playback_write, playback_stop, playback_destroy};
static int usb_playback_init(void **context, int rate, int channels, int bits) {
    assert(rate == 16000 && channels == 1 && bits == 16);
    *context = &usb_pb_active;
    return 0;
}
static const mybot_audio_playback_ops_t usb_playback_ops = {
    usb_playback_init, playback_start, playback_write, playback_stop, playback_destroy};
static void audio_error(void *context) {
    assert(context == &pipeline);
    audio_errors++;
}
static const mybot_audio_volume_ops_t volume_ops = {volume_init, volume_set, volume_get,
                                                    volume_destroy};

static int send_audio(const void *data, size_t bytes, void *context) {
    assert(bytes == 960 * (MYBOT_CLOUD_AEC ? 2U : 1U) * sizeof(int16_t));
    if (context == &meeting_usb) {
        assert(current_queue == USB_SEND_QUEUE);
        memcpy(usb_sent, data, bytes);
        usb_sends++;
    } else {
        assert(context == &pipeline);
        memcpy(sent, data, bytes);
        sends++;
    }
    return 0;
}

int mybot_announce_init(mybot_announce_t *announce) {
    announce_inits++;
    memset(announce, 0, sizeof(*announce));
    return 0;
}

void mybot_announce_deinit(mybot_announce_t *announce) {
    (void)announce;
}

void mybot_announce_stop(mybot_announce_t *announce) {
    announce->generation++;
}

bool mybot_announce_is_active(mybot_announce_t *announce) {
    (void)announce;
    return false;
}

uint32_t mybot_announce_get_generation(mybot_announce_t *announce) {
    return announce->generation;
}

int mybot_announce_read_pcm(mybot_announce_t *announce, int16_t *pcm, int frames,
                            uint32_t *generation) {
    (void)announce;
    (void)pcm;
    (void)frames;
    (void)generation;
    abort();
}

int mybot_announce_play_pair_code(mybot_announce_t *announce, const char *code) {
    (void)announce;
    (void)code;
    abort();
}

aosl_mpq_t aosl_mpq_create_flags(int flags, int priority, int stack, int maximum, const char *name,
                                 void *init, void *fini, void *context) {
    (void)flags;
    (void)priority;
    (void)stack;
    (void)maximum;
    if (!strcmp(name, "cap_mpq") && fail_cap_create)
        return AOSL_MPQ_INVALID;
    aosl_mpq_t queue = next_queue++;
    assert(queue > 0 && queue < QUEUE_COUNT);
    workers[queue].fini = (void (*)(void *))fini;
    workers[queue].context = context;
    current_queue = queue;
    assert(((int (*)(void *))init)(context) == 0);
    current_queue = AOSL_MPQ_INVALID;
    return queue;
}

aosl_timer_t aosl_mpq_set_timer(uintptr_t interval, aosl_timer_func_t callback, void *destructor,
                                uintptr_t argc, ...) {
    (void)destructor;
    assert(interval == 60 && argc == 1 && current_queue > 0);
    va_list args;
    va_start(args, argc);
    workers[current_queue].argument = va_arg(args, uintptr_t);
    va_end(args);
    workers[current_queue].callback = callback;
    workers[current_queue].active = true;
    return (aosl_timer_t)current_queue;
}

int aosl_mpq_kill_timer(aosl_timer_t timer) {
    assert(timer > 0 && timer < QUEUE_COUNT);
    workers[timer].active = false;
    return 0;
}

static void run_call(const queued_call_t *call) {
    aosl_mpq_t previous = current_queue;
    current_queue = call->queue;
    uintptr_t args[3];
    memcpy(args, call->argv, sizeof(args));
    call->callback(NULL, NULL, call->argc, args);
    current_queue = previous;
}

static queued_call_t take_call(aosl_mpq_t queue, aosl_mpq_func_t callback, uintptr_t argc,
                               va_list args) {
    assert(queue > 0 && queue < QUEUE_COUNT && argc <= 3);
    queued_call_t call = {.callback = callback, .argc = argc, .queue = queue};
    for (uintptr_t i = 0; i < argc; ++i)
        call.argv[i] = va_arg(args, uintptr_t);
    return call;
}

static void tick(aosl_mpq_t queue) {
    assert(queue > 0 && queue < QUEUE_COUNT);
    if (!workers[queue].active)
        return;
    aosl_mpq_t previous = current_queue;
    current_queue = queue;
    uintptr_t argument = workers[queue].argument;
    workers[queue].callback((aosl_timer_t)queue, NULL, 1, &argument);
    current_queue = previous;
}

int aosl_mpq_queue(aosl_mpq_t queue, aosl_mpq_t source, aosl_ref_t ref, const char *name,
                   aosl_mpq_func_t callback, uintptr_t argc, ...) {
    (void)source;
    (void)ref;
    assert(strcmp(name, "pause_fence") == 0);
    va_list args;
    va_start(args, argc);
    queued_call_t call = take_call(queue, callback, argc, args);
    va_end(args);
    if (queue == fail_queue)
        return -1;
    if (tick_during_fence) {
        tick(CAPTURE_QUEUE);
        tick(PLAYBACK_QUEUE);
        tick(SEND_QUEUE);
        fence_ticks++;
    }
    if (queue == delay_queue) {
        assert(queued_count < MAX_QUEUED);
        queued[queued_count++] = call;
    } else {
        run_call(&call);
    }
    return 0;
}

int aosl_mpq_call(aosl_mpq_t queue, aosl_ref_t ref, const char *name, aosl_mpq_func_t callback,
                  uintptr_t argc, ...) {
    (void)ref;
    (void)name;
    if (check_reconnect_owner) {
        /* RTC callbacks must not wait on a send worker which can synchronously
         * call RTC. Only the control owner performs these consumer barriers. */
        assert(current_queue == CONTROL_QUEUE && current_queue != RTC_QUEUE);
        assert(!pipeline.rtc_connected && !meeting_usb.rtc_connected);
        assert(reconnect_call_count < sizeof(reconnect_calls) / sizeof(reconnect_calls[0]));
        reconnect_calls[reconnect_call_count++] = queue;
        if (queue == fail_call_queue)
            return -1;
    }
    va_list args;
    va_start(args, argc);
    queued_call_t call = take_call(queue, callback, argc, args);
    va_end(args);
    run_call(&call);
    return 0;
}

static void run_queued(aosl_mpq_t queue) {
    for (unsigned i = 0; i < queued_count;) {
        if (queued[i].queue != queue) {
            i++;
            continue;
        }
        queued_call_t call = queued[i];
        memmove(queued + i, queued + i + 1, (--queued_count - i) * sizeof(queued[0]));
        run_call(&call);
    }
}

int aosl_mpq_destroy_wait(aosl_mpq_t queue) {
    if (fail_destroy)
        return -1;
    run_queued(queue);
    workers[queue].fini(workers[queue].context);
    return 0;
}

void aosl_msleep(unsigned milliseconds) {
    waited_ms += milliseconds;
    if (release_producer_during_wait) {
        release_producer_during_wait = false;
        aosl_atomic_set(&pipeline.remote_audio_busy, false);
    }
    if (stop_during_wait) {
        stop_during_wait = false;
        assert(mybot_media_pipeline_stop(&pipeline) == 0);
    }
}

static void reset(void) {
    memset(&platform, 0, sizeof(platform));
    memset(workers, 0, sizeof(workers));
    memset(sent, 0, sizeof(sent));
    memset(&meeting_usb, 0, sizeof(meeting_usb));
    memset(usb_sent, 0, sizeof(usb_sent));
    usb_sends = 0;
    queued_count = 0;
    current_queue = fail_queue = delay_queue = AOSL_MPQ_INVALID;
    next_queue = CAPTURE_QUEUE;
    fail_pb_stop = fail_pb_start = stop_during_wait = release_producer_during_wait = false;
    fail_destroy = tick_during_fence = false;
    hold_remote_producer = producer_acquired = release_remote_producer = false;
    disconnect_during_capture = check_reconnect_owner = false;
    fail_call_queue = AOSL_MPQ_INVALID;
    reconnect_call_count = 0;
    cap_active = pb_active = usb_active = usb_pb_active = false;
    usb_disconnected = pause_usb_write = false;
    physical_playback_fault = pause_physical_write = stop_physical_write = false;
    audio_errors = volume_inits = announce_inits = 0;
    fail_usb_start = fail_cap_stop = fail_codec_start = fail_cap_create = false;
    usb_destroyed = codec_destroyed = 0;
    played_frames = sends = capture_reads = playback_writes = pb_stops = cap_stops = 0;
    hardware_volume_sets = waited_ms = fence_ticks = 0;
    captured_sample = 1234;
    usb_captured_sample = 5678;
    hardware_volume = 45;
    capture_frames = write_limit = MYBOT_MEDIA_FRAME_SAMPLES;
    platform.audio_capture = &capture_ops;
    platform.audio_usb_capture = &usb_ops;
    platform.audio_usb_playback = &usb_playback_ops;
    platform.audio_playback = &playback_ops;
    platform.audio_volume = &volume_ops;
    mybot_media_pipeline_init(&pipeline);
    mybot_media_pipeline_callbacks_t callbacks = {
        .send_audio = send_audio, .on_audio_error = audio_error, .user_data = &pipeline};
    assert(mybot_media_pipeline_start(&pipeline, &callbacks) == 0);
    mybot_media_pipeline_set_rtc_connected(&pipeline, true);
}

static void finish(void) {
    fail_pb_stop = fail_pb_start = fail_destroy = fail_cap_stop = false;
    assert(mybot_media_pipeline_stop(&pipeline) == 0);
    assert(mybot_media_pipeline_destroy(&pipeline) == 0);
    assert(queued_count == 0);
}

static void push_to(mybot_media_pipeline_t *target, int16_t value) {
    int16_t remote[MYBOT_MEDIA_FRAME_SAMPLES];
    for (unsigned i = 0; i < MYBOT_MEDIA_FRAME_SAMPLES; ++i)
        remote[i] = value;
    mybot_media_pipeline_push_remote_audio(target, remote, sizeof(remote));
}

static void push(int16_t value) {
    push_to(&pipeline, value);
}

static void expect_samples(const int16_t *samples, int16_t mic, int16_t reference) {
    for (unsigned i = 0; i < MYBOT_MEDIA_FRAME_SAMPLES; ++i) {
#if MYBOT_CLOUD_AEC
        assert(samples[i * 2] == mic);
        assert(samples[i * 2 + 1] == reference);
#else
        (void)reference;
        assert(samples[i] == mic);
#endif
    }
}

static void expect_sent(int16_t mic, int16_t reference) {
    assert(sends > 0);
    expect_samples(sent, mic, reference);
}

static void start_meeting_pair(void) {
    reset();
    mybot_media_pipeline_init(&meeting_usb);
    mybot_media_pipeline_callbacks_t callbacks = {.send_audio = send_audio,
                                                  .user_data = &meeting_usb};
    assert(mybot_media_pipeline_start_usb(&meeting_usb, &callbacks) == 0);
    mybot_media_pipeline_set_rtc_connected(&meeting_usb, true);
    assert(meeting_usb.cap_mpq == USB_CAPTURE_QUEUE && meeting_usb.pb_mpq == USB_PLAYBACK_QUEUE &&
           meeting_usb.send_mpq == USB_SEND_QUEUE);
}

static void finish_meeting_pair(void) {
    assert(mybot_media_pipeline_stop(&meeting_usb) == 0);
    assert(mybot_media_pipeline_destroy(&meeting_usb) == 0);
    finish();
}

static void seed_reconnect_backlogs(void) {
    captured_sample = 999;
    usb_captured_sample = 888;
    for (unsigned frame = 0; frame < 10; ++frame) {
        tick(CAPTURE_QUEUE);
        tick(USB_CAPTURE_QUEUE);
    }
    /* Keep both unplayed RTC PCM and a partially admitted playback block.
     * Physical playback also supplies an old, incomplete AEC reference. */
    write_limit = 240;
    push(2500);
    tick(PLAYBACK_QUEUE);
    push(3500);
    push_to(&meeting_usb, 4500);
    tick(USB_PLAYBACK_QUEUE);
    push_to(&meeting_usb, 5500);
    assert(pipeline.pb_pending_frames == 720 && meeting_usb.pb_pending_frames == 720);
    assert(mybot_ringbuf_get_data_size(pipeline.cap_ringbuf) == 10 * MYBOT_MEDIA_FRAME_BYTES);
    assert(mybot_ringbuf_get_data_size(meeting_usb.cap_ringbuf) == 10 * MYBOT_MEDIA_FRAME_BYTES);
    disconnect_during_capture = true;
    tick(CAPTURE_QUEUE); /* The read started before ready closed; its result must be discarded. */
    assert(!disconnect_during_capture && !pipeline.rtc_connected && !meeting_usb.rtc_connected);
    assert(mybot_ringbuf_get_data_size(pipeline.cap_ringbuf) == 10 * MYBOT_MEDIA_FRAME_BYTES);
    write_limit = MYBOT_MEDIA_FRAME_SAMPLES;
}

static int recover_meeting_from_control(void) {
    aosl_mpq_t previous = current_queue;
    current_queue = CONTROL_QUEUE;
    reconnect_call_count = 0;
    check_reconnect_owner = true;
    int physical = mybot_media_pipeline_flush_session(&pipeline);
    int usb = mybot_media_pipeline_flush_session(&meeting_usb);
    check_reconnect_owner = false;
    assert(reconnect_call_count == 6);
    for (unsigned i = 0; i < reconnect_call_count; ++i)
        assert(reconnect_calls[i] == (aosl_mpq_t)(i + 1));
    if (physical == 0 && usb == 0) {
        mybot_media_pipeline_set_rtc_connected(&pipeline, true);
        mybot_media_pipeline_set_rtc_connected(&meeting_usb, true);
    }
    current_queue = previous;
    return physical < 0 || usb < 0 ? -1 : 0;
}

static void expect_session_queues_empty(mybot_media_pipeline_t *target) {
    assert(!mybot_ringbuf_get_data_size(target->cap_ringbuf));
    assert(!mybot_ringbuf_get_data_size(target->pb_ringbuf));
    assert(!target->pb_pending_frames && !target->pb_pending_offset);
#if MYBOT_CLOUD_AEC
    if (target->ref_ringbuf)
        assert(!mybot_ringbuf_get_data_size(target->ref_ringbuf));
#endif
}

static void send_fresh_meeting_audio(void) {
    captured_sample = 1234;
    usb_captured_sample = 8765;
    push(6000);
    tick(PLAYBACK_QUEUE);
    push_to(&meeting_usb, 7000);
    tick(USB_PLAYBACK_QUEUE);
    tick(CAPTURE_QUEUE);
    tick(USB_CAPTURE_QUEUE);
    tick(SEND_QUEUE);
    tick(USB_SEND_QUEUE);
    assert(sends == 1 && usb_sends == 1);
    expect_sent(1234, 6000);
    expect_samples(usb_sent, 8765, 0);
}

static void test_reconnect_flushes_both_routes_before_ready(void) {
    start_meeting_pair();
    seed_reconnect_backlogs();
    unsigned admitted_device_frames = played_frames;
    assert(recover_meeting_from_control() == 0);
    expect_session_queues_empty(&pipeline);
    expect_session_queues_empty(&meeting_usb);
    /* Session flush cannot retract PCM already accepted by a device or RTC. */
    assert(played_frames == admitted_device_frames && !pb_stops && !cap_stops);
    assert(!pipeline.paused && !meeting_usb.paused && !sends && !usb_sends);
    send_fresh_meeting_audio();
    finish_meeting_pair();
    tests++;
}

static void test_reconnect_flush_failure_keeps_both_routes_closed(void) {
    start_meeting_pair();
    seed_reconnect_backlogs();
    fail_call_queue = SEND_QUEUE;
    assert(recover_meeting_from_control() < 0);
    assert(!pipeline.rtc_connected && !meeting_usb.rtc_connected);
    assert(mybot_ringbuf_get_data_size(pipeline.cap_ringbuf) == 10 * MYBOT_MEDIA_FRAME_BYTES);
    expect_session_queues_empty(&meeting_usb); /* A failure never skips the other route. */
    tick(SEND_QUEUE);
    tick(USB_SEND_QUEUE);
    assert(!sends && !usb_sends);
    fail_call_queue = AOSL_MPQ_INVALID;
    assert(recover_meeting_from_control() == 0);
    expect_session_queues_empty(&pipeline);
    expect_session_queues_empty(&meeting_usb);
    send_fresh_meeting_audio();
    finish_meeting_pair();
    tests++;
}

static void test_reconnect_flush_preserves_user_pause(void) {
    start_meeting_pair();
    assert(mybot_media_pipeline_set_paused(&pipeline, true) == 0);
    assert(mybot_media_pipeline_set_paused(&meeting_usb, true) == 0);
    int16_t old[MYBOT_MEDIA_FRAME_SAMPLES];
    for (unsigned i = 0; i < MYBOT_MEDIA_FRAME_SAMPLES; ++i)
        old[i] = 9999;
    mybot_media_pipeline_t *routes[] = {&pipeline, &meeting_usb};
    for (unsigned i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
        mybot_media_pipeline_t *target = routes[i];
        /* Model publications admitted before pause/reconnect closed admission. */
        assert(mybot_ringbuf_write(target->cap_ringbuf, (const char *)old, sizeof(old)) > 0);
        assert(mybot_ringbuf_write(target->pb_ringbuf, (const char *)old, sizeof(old)) > 0);
#if MYBOT_CLOUD_AEC
        if (target->ref_ringbuf)
            assert(mybot_ringbuf_write(target->ref_ringbuf, (const char *)old, sizeof(old)) > 0);
#endif
        target->pb_pending_frames = 240;
        mybot_media_pipeline_set_rtc_connected(target, false);
    }
    assert(recover_meeting_from_control() == 0);
    expect_session_queues_empty(&pipeline);
    expect_session_queues_empty(&meeting_usb);
    assert(pipeline.paused && meeting_usb.paused);
    assert(pipeline.playback_suspended && meeting_usb.playback_suspended);
    assert(!pb_active && !usb_pb_active && cap_active && usb_active);
    tick(CAPTURE_QUEUE);
    tick(USB_CAPTURE_QUEUE);
    tick(SEND_QUEUE);
    tick(USB_SEND_QUEUE);
    assert(sends == 1 && usb_sends == 1);
    expect_sent(0, 0);
    expect_samples(usb_sent, 0, 0);
    finish_meeting_pair();
    tests++;
}

static void test_unchanged_audio(void) {
    reset();
    tick(SEND_QUEUE);
    assert(sends == 0);
    push(2500);
    tick(PLAYBACK_QUEUE);
    assert(playback_writes == 1 && played_frames == 960 && played[0] == 2500);
    tick(CAPTURE_QUEUE);
    tick(SEND_QUEUE);
    expect_sent(1234, 2500);
    assert(!mybot_media_pipeline_is_paused(&pipeline));
    assert(pb_stops == 0 && cap_stops == 0 && hardware_volume_sets == 0);
    finish();
    tests++;
}

static void test_pause_silent_cadence_and_playback_discard(void) {
    reset();
    write_limit = 240;
    push(3000);
    tick(PLAYBACK_QUEUE);
    push(4000);
    tick(CAPTURE_QUEUE);
    assert(pipeline.pb_pending_frames == 720 && played_frames == 240);
    assert(mybot_media_pipeline_set_paused(&pipeline, true) == 0);
    assert(mybot_media_pipeline_is_paused(&pipeline));
    assert(aosl_atomic_read(&pipeline.rtc_connected) && cap_active && !pb_active);
    assert(played_frames == 0 && pipeline.pb_pending_frames == 0);
    assert(mybot_ringbuf_get_data_size(pipeline.pb_ringbuf) == 0);
    assert(mybot_ringbuf_get_data_size(pipeline.cap_ringbuf) == 0);
#if MYBOT_CLOUD_AEC
    assert(mybot_ringbuf_get_data_size(pipeline.ref_ringbuf) == 0);
#endif
    unsigned previous_writes = playback_writes;
    for (unsigned frame = 0; frame < 4; ++frame) {
        push(7777);
        tick(CAPTURE_QUEUE);
        tick(PLAYBACK_QUEUE);
        tick(SEND_QUEUE);
        expect_sent(0, 0);
        assert(sends == frame + 1);
    }
    /* Silent packets continue even if the capture device temporarily has no data. */
    capture_frames = 0;
    tick(CAPTURE_QUEUE);
    tick(SEND_QUEUE);
    assert(sends == 5 && capture_reads == 6);
    expect_sent(0, 0);
    assert(playback_writes == previous_writes && cap_stops == 0);
    assert(hardware_volume == 45 && hardware_volume_sets == 0);
    finish();
    tests++;
}

static void test_resume_flushes_old_audio(void) {
    reset();
    assert(mybot_media_pipeline_set_paused(&pipeline, true) == 0);
    /* Model PCM published by callbacks/worker operations already in flight. */
    int16_t old[MYBOT_MEDIA_FRAME_SAMPLES];
    for (unsigned i = 0; i < MYBOT_MEDIA_FRAME_SAMPLES; ++i)
        old[i] = 8888;
    assert(mybot_ringbuf_write(pipeline.pb_ringbuf, (const char *)old, sizeof(old)) > 0);
    assert(mybot_ringbuf_write(pipeline.cap_ringbuf, (const char *)old, sizeof(old)) > 0);
#if MYBOT_CLOUD_AEC
    assert(mybot_ringbuf_write(pipeline.ref_ringbuf, (const char *)old, sizeof(old)) > 0);
#endif
    pipeline.pb_pending_frames = 100;
    tick_during_fence = true;
    assert(mybot_media_pipeline_set_paused(&pipeline, false) == 0);
    assert(fence_ticks == 3 && !mybot_media_pipeline_is_paused(&pipeline));
    assert(pb_active && cap_active && played_frames == 0);
    unsigned previous_writes = playback_writes;
    tick(PLAYBACK_QUEUE);
    tick(SEND_QUEUE);
    assert(playback_writes == previous_writes && sends == 3);
    push(6000);
    tick(PLAYBACK_QUEUE);
    tick(CAPTURE_QUEUE);
    tick(SEND_QUEUE);
    expect_sent(1234, 6000);
    assert(played_frames == 960 && played[0] == 6000);
    finish();
    tests++;
}

static void test_repeated_pause_resume(void) {
    reset();
    assert(mybot_media_pipeline_set_paused(&pipeline, false) == 0 && pb_stops == 0);
    for (unsigned iteration = 0; iteration < 20; ++iteration) {
        assert(mybot_media_pipeline_set_paused(&pipeline, true) == 0);
        assert(mybot_media_pipeline_set_paused(&pipeline, true) == 0);
        push(9000);
        tick(SEND_QUEUE);
        expect_sent(0, 0);
        assert(mybot_media_pipeline_set_paused(&pipeline, false) == 0);
        assert(!mybot_media_pipeline_is_paused(&pipeline));
        assert(mybot_media_pipeline_set_paused(&pipeline, false) == 0);
        tick(CAPTURE_QUEUE);
        tick(SEND_QUEUE);
        expect_sent(1234, 0);
    }
    assert(hardware_volume_sets == 0 && hardware_volume == 45 && cap_stops == 0);
    finish();
    tests++;
}

static void test_device_failures_stay_paused(void) {
    reset();
    push(6500);
    tick(PLAYBACK_QUEUE);
    fail_pb_stop = true;
    assert(mybot_media_pipeline_set_paused(&pipeline, true) < 0);
    assert(mybot_media_pipeline_is_paused(&pipeline));
    tick(SEND_QUEUE);
    /* Failed device stop can leave actual playback in flight. Preserve its
     * reference even while the MIC has already become silent. */
    expect_sent(0, 6500);
    assert(mybot_media_pipeline_set_paused(&pipeline, false) < 0);
    assert(mybot_media_pipeline_is_paused(&pipeline));
    fail_pb_stop = false;
    fail_pb_start = true;
    assert(mybot_media_pipeline_set_paused(&pipeline, false) < 0);
    assert(mybot_media_pipeline_is_paused(&pipeline) && !pb_active && cap_active);
    fail_pb_start = false;
    assert(mybot_media_pipeline_set_paused(&pipeline, false) == 0);
    assert(!mybot_media_pipeline_is_paused(&pipeline) && pb_active);
    finish();
    tests++;
}

static void test_worker_fence_failure_and_timeout(void) {
    for (aosl_mpq_t queue = CAPTURE_QUEUE; queue <= SEND_QUEUE; ++queue) {
        reset();
        fail_queue = queue;
        assert(mybot_media_pipeline_set_paused(&pipeline, true) < 0);
        assert(mybot_media_pipeline_is_paused(&pipeline));
        assert(mybot_media_pipeline_set_paused(&pipeline, false) < 0);
        assert(mybot_media_pipeline_is_paused(&pipeline));
        fail_queue = AOSL_MPQ_INVALID;
        delay_queue = queue;
        assert(mybot_media_pipeline_set_paused(&pipeline, false) < 0);
        assert(waited_ms == 1000 && queued_count == 1);
        assert(mybot_media_pipeline_is_paused(&pipeline) && !pb_active);
        delay_queue = AOSL_MPQ_INVALID;
        assert(mybot_media_pipeline_set_paused(&pipeline, false) == 0);
        /* A superseded late callback must not drain the resumed session. */
        push(4321);
        tick(CAPTURE_QUEUE);
        run_queued(queue);
        tick(PLAYBACK_QUEUE);
        tick(SEND_QUEUE);
        expect_sent(1234, 4321);
        finish();
    }
    tests++;
}

static void test_remote_producer_fence(void) {
    reset();
    aosl_atomic_set(&pipeline.remote_audio_busy, true);
    push(3000);
    assert(mybot_ringbuf_get_data_size(pipeline.pb_ringbuf) == 0);
    assert(mybot_media_pipeline_set_paused(&pipeline, true) < 0 && waited_ms == 1000);
    assert(mybot_media_pipeline_is_paused(&pipeline));
    release_producer_during_wait = true;
    assert(mybot_media_pipeline_set_paused(&pipeline, false) == 0);
    assert(waited_ms == 1005 && !aosl_atomic_read(&pipeline.remote_audio_busy));
    assert(!mybot_media_pipeline_is_paused(&pipeline));
    finish();
    tests++;
}

static void *remote_producer(void *context) {
    (void)context;
    push(3456);
    return NULL;
}

static void test_remote_callback_racing_pause(void) {
    reset();
    pthread_t producer;
    hold_remote_producer = true;
    assert(pthread_create(&producer, NULL, remote_producer, NULL) == 0);
    assert(pthread_mutex_lock(&producer_lock) == 0);
    while (!producer_acquired)
        assert(pthread_cond_wait(&producer_cond, &producer_lock) == 0);
    assert(pthread_mutex_unlock(&producer_lock) == 0);
    /* The callback passed the initial gate before pause but cannot publish
     * afterwards; its second gate check runs once this thread releases it. */
    assert(mybot_media_pipeline_set_paused(&pipeline, true) < 0);
    assert(mybot_media_pipeline_is_paused(&pipeline) && waited_ms == 1000);
    assert(pthread_mutex_lock(&producer_lock) == 0);
    release_remote_producer = true;
    assert(pthread_cond_signal(&producer_cond) == 0);
    assert(pthread_mutex_unlock(&producer_lock) == 0);
    assert(pthread_join(producer, NULL) == 0);
    assert(mybot_ringbuf_get_data_size(pipeline.pb_ringbuf) == 0);
    assert(!aosl_atomic_read(&pipeline.remote_audio_busy));
    assert(mybot_media_pipeline_set_paused(&pipeline, false) == 0);
    push(4567);
    tick(PLAYBACK_QUEUE);
    tick(CAPTURE_QUEUE);
    tick(SEND_QUEUE);
    expect_sent(1234, 4567);
    finish();
    tests++;
}

static void test_stop_and_delayed_fence_ownership(void) {
    reset();
    assert(mybot_media_pipeline_set_paused(&pipeline, true) == 0);
    delay_queue = PLAYBACK_QUEUE;
    stop_during_wait = true;
    assert(mybot_media_pipeline_set_paused(&pipeline, false) < 0);
    assert(!aosl_atomic_read(&pipeline.running));
    assert(!mybot_media_pipeline_is_paused(&pipeline) && !pb_active && !cap_active);
    assert(queued_count == 0);
    assert(mybot_media_pipeline_set_paused(&pipeline, true) < 0);
    assert(mybot_media_pipeline_destroy(&pipeline) == 0);

    reset();
    delay_queue = SEND_QUEUE;
    assert(mybot_media_pipeline_set_paused(&pipeline, true) < 0 && queued_count == 1);
    fail_destroy = true;
    assert(mybot_media_pipeline_stop(&pipeline) < 0);
    assert(mybot_media_pipeline_destroy(&pipeline) < 0);
    assert(pipeline.cap_ringbuf && pipeline.pb_ringbuf);
#if MYBOT_CLOUD_AEC
    assert(pipeline.ref_ringbuf);
#endif
    fail_destroy = false;
    finish();
    tests++;
}

static void test_session_end_and_new_session(void) {
    reset();
    fail_pb_stop = true;
    assert(mybot_media_pipeline_set_paused(&pipeline, true) < 0);
    assert(mybot_media_pipeline_end_session(&pipeline) < 0);
    assert(mybot_media_pipeline_is_paused(&pipeline));
    fail_pb_stop = false;
    fail_pb_start = true;
    assert(mybot_media_pipeline_end_session(&pipeline) < 0);
    assert(mybot_media_pipeline_is_paused(&pipeline) && !pb_active);
    fail_pb_start = false;
    assert(mybot_media_pipeline_end_session(&pipeline) == 0);
    assert(!mybot_media_pipeline_is_paused(&pipeline) && pb_active);
    assert(!aosl_atomic_read(&pipeline.rtc_connected));
    assert(mybot_media_pipeline_set_paused(&pipeline, true) < 0);
    mybot_media_pipeline_set_rtc_connected(&pipeline, true);
    tick(CAPTURE_QUEUE);
    tick(SEND_QUEUE);
    expect_sent(1234, 0);
    assert(hardware_volume_sets == 0 && hardware_volume == 45);
    finish();
    tests++;
}

static void test_invalid_arguments(void) {
    mybot_media_pipeline_t unstarted;
    mybot_media_pipeline_init(&unstarted);
    assert(mybot_media_pipeline_set_paused(NULL, true) < 0);
    assert(mybot_media_pipeline_set_paused(&unstarted, true) < 0);
    assert(!mybot_media_pipeline_is_paused(NULL));
    reset();
    aosl_atomic_set(&pipeline.pause_control_busy, true);
    assert(mybot_media_pipeline_set_paused(&pipeline, true) < 0);
    assert(!mybot_media_pipeline_is_paused(&pipeline));
    aosl_atomic_set(&pipeline.pause_control_busy, false);
    finish();
    tests++;
}

static void reset_usb(void) {
    reset();
    finish();
    next_queue = CAPTURE_QUEUE;
    volume_inits = announce_inits = 0;
    usb_destroyed = codec_destroyed = 0;
    cap_active = true; /* A separate physical pipeline continues to own the codec. */
    mybot_media_pipeline_init(&pipeline);
}
static int start_usb(void) {
    mybot_media_pipeline_callbacks_t callbacks = {
        .send_audio = send_audio, .on_audio_error = audio_error, .user_data = &pipeline};
    return mybot_media_pipeline_start_usb(&pipeline, &callbacks);
}
static void test_usb_pipeline_zero_reference_and_unity_gain(void) {
    reset_usb();
    assert(start_usb() == 0);
    assert(usb_active && usb_pb_active && cap_active && !pb_active);
    assert(!codec_destroyed && !volume_inits && !announce_inits);
    assert(pipeline.usb && !pipeline.audio.volume_active);
#if MYBOT_CLOUD_AEC
    assert(!pipeline.ref_ringbuf);
#endif
    assert(mybot_audio_get_media_volume(&pipeline.audio) == 100);
    mybot_media_pipeline_set_rtc_connected(&pipeline, true);
    push(2500);
    tick(PLAYBACK_QUEUE);
    assert(played_frames == 960 &&
           played[0] == 2500); /* Physical volume45 must not attenuate USB. */
    tick(CAPTURE_QUEUE);
    tick(SEND_QUEUE);
    expect_sent(5678, 0); /* SPEAK return never enters the LISTEN AEC reference. */
    assert(mybot_media_pipeline_set_paused(&pipeline, true) == 0);
    assert(!usb_pb_active && !played_frames && usb_active && cap_active);
    tick(CAPTURE_QUEUE);
    tick(SEND_QUEUE);
    expect_sent(0, 0);
    assert(mybot_media_pipeline_set_paused(&pipeline, false) == 0);
    tick(CAPTURE_QUEUE);
    tick(SEND_QUEUE);
    expect_sent(5678, 0);
    usb_disconnected = true;
    tick(CAPTURE_QUEUE);
    assert(audio_errors == 1);
    tick(CAPTURE_QUEUE);
    assert(audio_errors == 1); /* A latched capture fault does not notify every frame. */
    usb_disconnected = false;
    pause_usb_write = true;
    push(3456);
    tick(PLAYBACK_QUEUE);
    assert(audio_errors == 1); /* A write interrupted by pause is not USB detachment. */
    finish();
    assert(cap_active && !usb_active && !usb_pb_active && !codec_destroyed && usb_destroyed == 1);
    tests++;
}
static void test_usb_start_failures_and_retained_worker(void) {
    reset_usb();
    platform.audio_usb_playback = NULL;
    assert(start_usb() < 0 && cap_active && !usb_active && !usb_pb_active);
    reset_usb();
    fail_usb_start = true;
    assert(start_usb() < 0 && cap_active && !usb_active && !usb_pb_active);
    assert(usb_destroyed == 1 && !codec_destroyed && !volume_inits && !announce_inits);
    reset_usb();
    fail_cap_create = true;
    fail_destroy = true;
    assert(start_usb() < 0 && pipeline.cap_ctx && pipeline.pb_ctx && pipeline.cap_ringbuf);
    assert(!pipeline.stop_complete && cap_active && !usb_active && !usb_pb_active);
    assert(!usb_destroyed && !codec_destroyed);
    assert(mybot_media_pipeline_destroy(&pipeline) < 0);
    fail_destroy = false;
    fail_cap_create = false;
    finish();
    assert(usb_destroyed == 1 && !codec_destroyed && cap_active);
    tests++;
}

static void test_disconnected_quiesce_clears_device_pcm(void) {
    reset();
    push(3000);
    tick(PLAYBACK_QUEUE);
    assert(played_frames == 960);
    mybot_media_pipeline_set_rtc_connected(&pipeline, false);
    assert(mybot_media_pipeline_set_paused(&pipeline, true) < 0);
    fail_pb_stop = true;
    assert(mybot_media_pipeline_quiesce(&pipeline) < 0);
    assert(pipeline.paused && pipeline.playback_suspended && played_frames == 960);
    fail_pb_stop = false;
    assert(mybot_media_pipeline_quiesce(&pipeline) == 0);
    assert(!played_frames && !pb_active && cap_active && pipeline.paused);
    assert(mybot_media_pipeline_end_session(&pipeline) == 0);
    assert(!pipeline.paused && pb_active && cap_active && !pipeline.playback_suspended);
    finish();
    tests++;
}

static void test_physical_playback_fault_recovers_at_session_end(void) {
    reset();
    physical_playback_fault = true; /* The physical PCM worker retains a hard I2S fault. */
    push(2500);
    tick(PLAYBACK_QUEUE);
    assert(pipeline.playback_fault && audio_errors == 1 && !played_frames);
    tick(PLAYBACK_QUEUE);
    assert(audio_errors == 1 && playback_writes == 1);
    assert(mybot_media_pipeline_end_session(&pipeline) == 0);
    assert(!pipeline.playback_fault && !physical_playback_fault && pb_stops == 1);
    assert(cap_active && !cap_stops && hardware_volume == 45 && !hardware_volume_sets);
    mybot_media_pipeline_set_rtc_connected(&pipeline, true);
    push(3000);
    tick(PLAYBACK_QUEUE);
    assert(played_frames == 960 && played[0] == 3000);
    finish();
    tests++;
}

static void test_physical_playback_fault_recovery_retries(void) {
    reset();
    physical_playback_fault = true;
    push(2500);
    tick(PLAYBACK_QUEUE);
    fail_pb_stop = true;
    assert(mybot_media_pipeline_end_session(&pipeline) < 0);
    assert(pipeline.playback_fault && physical_playback_fault && pb_active && cap_active);
    fail_pb_stop = false;
    fail_pb_start = true;
    assert(mybot_media_pipeline_end_session(&pipeline) < 0);
    assert(pipeline.playback_fault && physical_playback_fault && !pb_active && cap_active);
    fail_pb_start = false;
    assert(mybot_media_pipeline_end_session(&pipeline) == 0);
    assert(!pipeline.playback_fault && !physical_playback_fault && pb_active && cap_active);
    mybot_media_pipeline_set_rtc_connected(&pipeline, true);
    push(3500);
    tick(PLAYBACK_QUEUE);
    assert(played_frames == 960 && played[0] == 3500);
    assert(audio_errors == 1 && !cap_stops && !hardware_volume_sets);
    finish();
    tests++;
}

static void test_physical_capture_fault_reports_once_and_recovers(void) {
    reset();
    capture_frames = -1;
    tick(CAPTURE_QUEUE);
    assert(pipeline.capture_fault && audio_errors == 1 && capture_reads == 1);
    tick(CAPTURE_QUEUE);
    assert(capture_reads == 1 && audio_errors == 1);
    capture_frames = MYBOT_MEDIA_FRAME_SAMPLES;
    assert(mybot_media_pipeline_end_session(&pipeline) == 0);
    assert(!pipeline.capture_fault && cap_active && cap_stops == 1);
    mybot_media_pipeline_set_rtc_connected(&pipeline, true);
    tick(CAPTURE_QUEUE);
    tick(SEND_QUEUE);
    expect_sent(captured_sample, 0);
    finish();
    tests++;
}

static void test_physical_capture_fault_recovery_retries(void) {
    reset();
    capture_frames = -1;
    tick(CAPTURE_QUEUE);
    fail_cap_stop = true;
    assert(mybot_media_pipeline_end_session(&pipeline) < 0);
    assert(pipeline.capture_fault && cap_active && cap_stops == 1);
    fail_cap_stop = false;
    fail_codec_start = true;
    assert(mybot_media_pipeline_end_session(&pipeline) < 0);
    assert(pipeline.capture_fault && !cap_active);
    fail_codec_start = false;
    assert(mybot_media_pipeline_end_session(&pipeline) == 0);
    assert(!pipeline.capture_fault && cap_active);
    capture_frames = MYBOT_MEDIA_FRAME_SAMPLES;
    mybot_media_pipeline_set_rtc_connected(&pipeline, true);
    tick(CAPTURE_QUEUE);
    tick(SEND_QUEUE);
    expect_sent(captured_sample, 0);
    finish();
    tests++;
}

static void test_physical_write_interrupted_by_pause_or_stop(void) {
    for (unsigned stopping = 0; stopping < 2; ++stopping) {
        reset();
        pause_physical_write = !stopping;
        stop_physical_write = stopping;
        push(2500);
        tick(PLAYBACK_QUEUE);
        assert(!pipeline.playback_fault && !audio_errors);
        finish();
    }
    tests++;
}

int main(void) {
    test_reconnect_flushes_both_routes_before_ready();
    test_reconnect_flush_failure_keeps_both_routes_closed();
    test_reconnect_flush_preserves_user_pause();
    test_physical_playback_fault_recovers_at_session_end();
    test_physical_playback_fault_recovery_retries();
    test_physical_capture_fault_reports_once_and_recovers();
    test_physical_capture_fault_recovery_retries();
    test_physical_write_interrupted_by_pause_or_stop();
    test_disconnected_quiesce_clears_device_pcm();
    test_usb_pipeline_zero_reference_and_unity_gain();
    test_usb_start_failures_and_retained_worker();
    assert(MYBOT_AUDIO_PTIME_MS == 60);
    test_unchanged_audio();
    test_pause_silent_cadence_and_playback_discard();
    test_resume_flushes_old_audio();
    test_repeated_pause_resume();
    test_device_failures_stay_paused();
    test_worker_fence_failure_and_timeout();
    test_remote_producer_fence();
    test_remote_callback_racing_pause();
    test_stop_and_delayed_fence_ownership();
    test_session_end_and_new_session();
    test_invalid_arguments();
    printf("media pause: %u tests passed (UBSan, cloud AEC=%d)\n", tests, MYBOT_CLOUD_AEC);
    return 0;
}
