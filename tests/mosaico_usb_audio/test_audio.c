/* SPDX-License-Identifier: Apache-2.0 */
#include "usb/mosaico_usb_audio.h"
#include "usb/mosaico_usb_descriptors.h"
#include "usb/mosaico_usb_resampler.h"
#include "device/dcd.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

#include <assert.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static unsigned transfer_calls, event_calls, log_calls;
static bool mounted, suspended, transfer_result = true;
static tusb_speed_t speed = TUSB_SPEED_HIGH;
static uint32_t feedback_value;
static uint8_t last_endpoint, last_port;
static uint8_t *last_buffer;
static uint16_t last_bytes;
static bool last_isr;
static const dcd_event_t *last_event;
static char logs[4096];
static int16_t fifo[960];
static unsigned fifo_bytes, fifo_offset;
static bool fail_fifo;
static _Thread_local bool pause_next_unlock;
static pthread_mutex_t barrier_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t barrier_cond = PTHREAD_COND_INITIALIZER;
static bool reader_paused, reader_released;

bool __wrap_dcd_edpt_xfer(uint8_t, uint8_t, uint8_t *, uint16_t);
bool __wrap_usbd_edpt_xfer_fifo(uint8_t, uint8_t, tu_fifo_t *, uint16_t);
bool tud_audio_tx_done_isr(uint8_t, uint16_t, uint8_t, uint8_t, uint8_t);
void __wrap_dcd_event_handler(const dcd_event_t *, bool);
bool tud_audio_rx_done_isr(uint8_t, uint16_t, uint8_t, uint8_t, uint8_t);
void tud_audio_feedback_params_cb(uint8_t, uint8_t, audio_feedback_params_t *);
bool tud_audio_set_req_entity_cb(uint8_t, tusb_control_request_t const *, uint8_t *);
bool tud_audio_get_req_entity_cb(uint8_t, tusb_control_request_t const *);
bool tud_audio_set_itf_cb(uint8_t, tusb_control_request_t const *);
bool tud_audio_set_itf_close_ep_cb(uint8_t, tusb_control_request_t const *);

void test_capture_unlock(portMUX_TYPE *lock) {
    assert(pthread_mutex_unlock(lock) == 0);
    if (!pause_next_unlock)
        return;
    pause_next_unlock = false;
    assert(pthread_mutex_lock(&barrier_lock) == 0);
    reader_paused = true;
    assert(pthread_cond_signal(&barrier_cond) == 0);
    while (!reader_released)
        assert(pthread_cond_wait(&barrier_cond, &barrier_lock) == 0);
    assert(pthread_mutex_unlock(&barrier_lock) == 0);
}

bool tud_mounted(void) {
    return mounted;
}
bool tud_suspended(void) {
    return suspended;
}
tusb_speed_t tud_speed_get(void) {
    return speed;
}
bool tud_audio_n_fb_set(uint8_t function, uint32_t value) {
    assert(function == 0);
    feedback_value = value;
    return true;
}
uint16_t tud_audio_n_available(uint8_t function) {
    assert(function == 0);
    return fifo_bytes - fifo_offset;
}
uint16_t tud_audio_n_read(uint8_t function, void *buffer, uint16_t bytes) {
    assert(function == 0 && bytes <= fifo_bytes - fifo_offset);
    if (fail_fifo)
        return 0;
    memcpy(buffer, (uint8_t *)fifo + fifo_offset, bytes);
    fifo_offset += bytes;
    return bytes;
}
bool tud_audio_n_clear_ep_out_ff(uint8_t function) {
    assert(function == 0);
    fifo_bytes = fifo_offset = 0;
    return true;
}
bool __real_dcd_edpt_xfer(uint8_t port, uint8_t endpoint, uint8_t *buffer, uint16_t bytes) {
    ++transfer_calls;
    last_port = port;
    last_endpoint = endpoint;
    last_buffer = buffer;
    last_bytes = bytes;
    return transfer_result;
}
bool usbd_edpt_xfer(uint8_t port, uint8_t endpoint, uint8_t *buffer, uint16_t bytes) {
    return __real_dcd_edpt_xfer(port, endpoint, buffer, bytes);
}
static tu_fifo_t *last_fifo;
bool __real_usbd_edpt_xfer_fifo(uint8_t port, uint8_t endpoint, tu_fifo_t *fifo, uint16_t bytes) {
    last_port = port;
    last_endpoint = endpoint;
    last_fifo = fifo;
    last_bytes = bytes;
    return transfer_result;
}
void __real_dcd_event_handler(const dcd_event_t *event, bool in_isr) {
    ++event_calls;
    last_event = event;
    last_isr = in_isr;
}
static unsigned control_gets;
static uint8_t control_reply[14];
static uint16_t control_length, control_wire_length;
static bool control_result = true;
bool tud_audio_buffer_and_schedule_control_xfer(uint8_t port, const tusb_control_request_t *request,
                                                void *data, uint16_t length) {
    assert(port == 0 && request && data && length <= sizeof(control_reply));
    ++control_gets;
    memcpy(control_reply, data, length);
    control_length = length;
    control_wire_length = length < request->wLength ? length : request->wLength;
    return control_result;
}
void test_usb_log(const char *tag, const char *format, ...) {
    assert(!strcmp(tag, "mosaico_uac"));
    ++log_calls;
    size_t offset = strlen(logs);
    va_list args;
    va_start(args, format);
    int bytes = vsnprintf(logs + offset, sizeof(logs) - offset, format, args);
    va_end(args);
    assert(bytes > 0 && (size_t)bytes + 1 < sizeof(logs) - offset);
    strcat(logs, "\n");
}
static void event(tinyusb_event_id_t id) {
    tinyusb_event_t value = {.id = id, .rhport = 0};
    mosaico_usb_audio_event(&value, NULL);
}
static void interface(bool open) {
    tusb_control_request_t request = {.wIndex = MYBOT_USB_ITF_SPEAKER, .wValue = open};
    assert(open ? tud_audio_set_itf_cb(0, &request) : tud_audio_set_itf_close_ep_cb(0, &request));
}
static void feed(unsigned frames, int16_t left, int16_t right) {
    while (frames) {
        unsigned amount = frames < 480 ? frames : 480;
        for (unsigned i = 0; i < amount; ++i) {
            fifo[i * 2] = left;
            fifo[i * 2 + 1] = right;
        }
        fifo_bytes = amount * 4;
        fifo_offset = 0;
        assert(tud_audio_rx_done_isr(0, fifo_bytes, 0, MYBOT_USB_EP_AUDIO_OUT, 1));
        assert(fifo_bytes == fifo_offset);
        frames -= amount;
    }
}
static void completed(uint8_t endpoint, uint8_t result, uint32_t bytes) {
    dcd_event_t value = {.rhport = 0,
                         .event_id = DCD_EVENT_XFER_COMPLETE,
                         .xfer_complete = {.ep_addr = endpoint, .result = result, .len = bytes}};
    __wrap_dcd_event_handler(&value, true);
    assert(last_event == &value && last_isr);
}
static void silent(const int16_t *samples, unsigned frames) {
    for (unsigned i = 0; i < frames; ++i)
        assert(samples[i] == 0);
}
static void control(unsigned channel, unsigned selector, int value);
static void test_transport(void) {
    mosaico_usb_audio_prepare();
    mosaico_usb_audio_prepare();
    mosaico_usb_audio_stats_t stats = mosaico_usb_audio_stats();
    assert(stats.prepared && !stats.ready && !stats.mounted);
    /* No periodic package microphone producer remains. Nominal silence is
     * generated even when TinyUSB schedules its initial zero-length packet. */
    tu_fifo_t unused_fifo = {0};
    assert(__wrap_usbd_edpt_xfer_fifo(0, MYBOT_USB_EP_AUDIO_IN, &unused_fifo, 0));
    assert(last_bytes == 24);
    silent((const int16_t *)last_buffer, 12);
    assert(tud_audio_tx_done_isr(0, 24, 0, MYBOT_USB_EP_AUDIO_IN, 1));
    assert(!tud_audio_tx_done_isr(0, 24, 0, MYBOT_USB_EP_CDC_IN, 1));
    speed = TUSB_SPEED_FULL;
    assert(__wrap_usbd_edpt_xfer_fifo(0, MYBOT_USB_EP_AUDIO_IN, &unused_fifo, 1920));
    assert(last_bytes == 192);
    silent((const int16_t *)last_buffer, 96);
    speed = TUSB_SPEED_HIGH;
    assert(__wrap_usbd_edpt_xfer_fifo(0, MYBOT_USB_EP_AUDIO_OUT, &unused_fifo, 196));
    assert(last_fifo == &unused_fifo && last_bytes == 196 &&
           last_endpoint == MYBOT_USB_EP_AUDIO_OUT);
    assert(__wrap_usbd_edpt_xfer_fifo(1, MYBOT_USB_EP_AUDIO_IN, &unused_fifo, 27));
    assert(last_port == 1 && last_bytes == 27);
    transfer_result = false;
    assert(!__wrap_usbd_edpt_xfer_fifo(0, MYBOT_USB_EP_AUDIO_IN, &unused_fifo, 0));
    transfer_result = true;
    control(0, AUDIO_FU_CTRL_MUTE, 1);
    stats = mosaico_usb_audio_stats();
    assert(stats.muted && stats.volume == 100);
    control(0, AUDIO_FU_CTRL_VOLUME, -25 * 256);
    control(1, AUDIO_FU_CTRL_VOLUME, -10 * 256);
    control(1, AUDIO_FU_CTRL_MUTE, 0);
    assert(mosaico_usb_audio_stats().volume == 50 && mosaico_usb_audio_stats().muted);
    control(0, AUDIO_FU_CTRL_MUTE, 0);
    feed(480, 1234, -1234);
    assert(mosaico_usb_audio_stats().discarded_bytes == 1920);
    fifo_bytes = 3;
    fifo_offset = 0;
    assert(tud_audio_rx_done_isr(0, 3, 0, MYBOT_USB_EP_AUDIO_OUT, 1));
    assert(fifo_bytes == 0 && mosaico_usb_audio_stats().output_errors == 1);
    fifo_bytes = 4;
    fail_fifo = true;
    assert(tud_audio_rx_done_isr(0, 4, 0, MYBOT_USB_EP_AUDIO_OUT, 1));
    assert(fifo_bytes == 0 && mosaico_usb_audio_stats().output_errors == 2);
    fail_fifo = false;
    assert(!tud_audio_rx_done_isr(0, 4, 0, MYBOT_USB_EP_CDC_OUT, 1));
    mounted = true;
    event(TINYUSB_EVENT_ATTACHED);
    assert(mosaico_usb_audio_stats().ready);
    suspended = true;
    event(TINYUSB_EVENT_SUSPENDED);
    assert(!mosaico_usb_audio_stats().ready);
    suspended = false;
    event(TINYUSB_EVENT_RESUMED);
    mounted = false;
    event(TINYUSB_EVENT_DETACHED);
    mosaico_usb_audio_event(NULL, NULL);
    tinyusb_event_t other = {.id = TINYUSB_EVENT_ATTACHED, .rhport = 1};
    mosaico_usb_audio_event(&other, NULL);
    audio_feedback_params_t feedback = {0};
    tud_audio_feedback_params_cb(0, 1, &feedback);
    assert(feedback.method == AUDIO_FEEDBACK_METHOD_DISABLED && feedback.sample_freq == 48000);
    assert(feedback_value == 393216);
    uint8_t fb[4] = {0x11, 0x22, 0x33, 0x44};
    assert(__wrap_dcd_edpt_xfer(0, MYBOT_USB_EP_AUDIO_FEEDBACK, fb, 4));
    assert(fb[0] == 0 && fb[1] == 0 && fb[2] == 6 && fb[3] == 0);
    assert(last_buffer == fb && last_port == 0 && last_endpoint == 0x81 && last_bytes == 4);
    transfer_result = false;
    assert(!__wrap_dcd_edpt_xfer(0, MYBOT_USB_EP_AUDIO_FEEDBACK, fb, 4));
    transfer_result = true;
    speed = TUSB_SPEED_FULL;
    tud_audio_feedback_params_cb(0, 1, &feedback);
    assert(feedback_value == 3145728);
    assert(__wrap_dcd_edpt_xfer(0, MYBOT_USB_EP_AUDIO_FEEDBACK, fb, 3));
    assert(fb[0] == 0 && fb[1] == 0 && fb[2] == 12);
    speed = TUSB_SPEED_HIGH;
    memset(fb, 0x5a, sizeof(fb));
    assert(__wrap_dcd_edpt_xfer(0, MYBOT_USB_EP_CDC_IN, fb, 4) && fb[0] == 0x5a);
    assert(last_endpoint == MYBOT_USB_EP_CDC_IN && last_buffer == fb);
    assert(__wrap_dcd_edpt_xfer(1, MYBOT_USB_EP_AUDIO_FEEDBACK, fb, 4) && fb[0] == 0x5a);
    assert(transfer_calls == 8);
    completed(MYBOT_USB_EP_AUDIO_OUT, XFER_RESULT_SUCCESS, 24);
    completed(MYBOT_USB_EP_AUDIO_OUT, XFER_RESULT_SUCCESS, 0);
    completed(MYBOT_USB_EP_AUDIO_OUT, XFER_RESULT_FAILED, 24);
    completed(MYBOT_USB_EP_AUDIO_IN, XFER_RESULT_SUCCESS, 24);
    completed(MYBOT_USB_EP_AUDIO_IN, XFER_RESULT_FAILED, 24);
    completed(MYBOT_USB_EP_AUDIO_FEEDBACK, XFER_RESULT_SUCCESS, 4);
    completed(MYBOT_USB_EP_AUDIO_FEEDBACK, XFER_RESULT_FAILED, 4);
    completed(MYBOT_USB_EP_CDC_OUT, XFER_RESULT_SUCCESS, 64);
    dcd_event_t unrelated = {.rhport = 1,
                             .event_id = DCD_EVENT_XFER_COMPLETE,
                             .xfer_complete = {.ep_addr = MYBOT_USB_EP_AUDIO_IN, .len = 999}};
    __wrap_dcd_event_handler(&unrelated, false);
    assert(last_event == &unrelated && !last_isr && event_calls == 9);
    stats = mosaico_usb_audio_stats();
    assert(stats.mounts == 1 && stats.unmounts == 1 && stats.suspends == 1 && stats.resumes == 1);
    assert(stats.input_callbacks == 3 && stats.silence_bytes == 240 && stats.input_errors == 1);
    assert(stats.rx_packets == 2 && stats.rx_bytes == 24 && stats.rx_zero_packets == 1 &&
           stats.out_errors == 1);
    assert(stats.mic_completed == 2 && stats.mic_bytes == 24 && stats.mic_errors == 1);
    assert(stats.fb_attempts == 3 && stats.fb_submitted == 2 && stats.fb_submit_errors == 1);
    assert(stats.fb_completed == 2 && stats.fb_bytes == 4 && stats.fb_errors == 1);
    mosaico_usb_audio_log_stats();
    assert(log_calls == 2 && strstr(logs, "routing=discard_and_silence"));
    puts("PASS mosaico USB transport silence, FIFO ownership, feedback and CDC forwarding");
}
static void control(unsigned channel, unsigned selector, int value) {
    uint8_t data[] = {(uint8_t)value, (uint8_t)(value >> 8)};
    tusb_control_request_t request = {.bRequest = AUDIO_CS_REQ_CUR,
                                      .wIndex = 0x0200,
                                      .wValue = (selector << 8) | channel,
                                      .wLength = selector == AUDIO_FU_CTRL_MUTE ? 1 : 2};
    assert(tud_audio_set_req_entity_cb(0, &request, data));
}
static void test_capture(void) {
    const mybot_audio_capture_ops_t *ops = mosaico_usb_audio_capture_ops();
    void *capture = NULL, *duplicate = NULL;
    assert(ops->init(&capture, 48000, 2, 16) < 0);
    assert(ops->init(&capture, 16000, 1, 16) == 0 && capture);
    assert(ops->init(&duplicate, 16000, 1, 16) < 0 && !duplicate);
    assert(ops->start(capture) < 0);
    mosaico_usb_audio_prepare();
    assert(ops->start(capture) < 0);
    mounted = true;
    assert(ops->start(capture) < 0);
    interface(true);
    assert(ops->start(capture) == 0);
    int16_t output[962];
    output[0] = output[961] = 12345;
    assert(ops->read(capture, output + 1, 960) == 960);
    silent(output + 1, 960);
    feed(2880, 24000, 0);
    assert(ops->read(capture, output + 1, 960) == 960);
    silent(output + 1, 320);
    assert(output[960] >= 11999 && output[960] <= 12001);
    assert(output[0] == 12345 && output[961] == 12345);
    mosaico_usb_audio_stats_t stats = mosaico_usb_audio_stats();
    assert(stats.capture_started && stats.speaker_open && stats.queued_frames == 960);
    assert(stats.capture_frames == 640 && stats.capture_zero_frames == 1280);
    /* Each 60 ms production block keeps the reserve, with unity nominal feedback. */
    for (unsigned i = 0; i < 300; ++i) {
        feed(2880, 24000, 0);
        assert(ops->read(capture, output + 1, 960) == 960);
        assert(mosaico_usb_audio_stats().queued_frames == 960);
    }
    uint8_t fb[4];
    assert(__wrap_dcd_edpt_xfer(0, MYBOT_USB_EP_AUDIO_FEEDBACK, fb, 4));
    assert(mosaico_usb_audio_stats().fb_value == 393216);
    feed(2400, 24000, 0); /* Host too slow: residual decreases -> request higher rate. */
    assert(ops->read(capture, output + 1, 960) == 960);
    assert(__wrap_dcd_edpt_xfer(0, MYBOT_USB_EP_AUDIO_FEEDBACK, fb, 4));
    assert(mosaico_usb_audio_stats().fb_value > 393216);
    feed(4000, 24000, 0); /* Host too fast: residual increases -> request lower rate. */
    assert(ops->read(capture, output + 1, 960) == 960);
    assert(__wrap_dcd_edpt_xfer(0, MYBOT_USB_EP_AUDIO_FEEDBACK, fb, 4));
    assert(mosaico_usb_audio_stats().fb_value < 393216);
    /* Master and per-channel controls affect only digital USB capture. */
    control(1, AUDIO_FU_CTRL_MUTE, 1);
    feed(2880, 24000, 0);
    assert(ops->read(capture, output + 1, 960) == 960);
    silent(output + 1, 960);
    control(1, AUDIO_FU_CTRL_MUTE, 0);
    control(0, AUDIO_FU_CTRL_VOLUME, -6 * 256);
    control(2, AUDIO_FU_CTRL_VOLUME, -6 * 256);
    feed(2880, 0, 24000);
    assert(ops->read(capture, output + 1, 960) == 960);
    assert(output[960] >= 3010 && output[960] <= 3020); /* -12 dB then mono average. */
    control(0, AUDIO_FU_CTRL_MUTE, 1);
    feed(2880, 24000, 24000);
    assert(ops->read(capture, output + 1, 960) == 960);
    silent(output + 1, 960);
    control(0, AUDIO_FU_CTRL_MUTE, 0);
    control(0, AUDIO_FU_CTRL_VOLUME, 0);
    control(2, AUDIO_FU_CTRL_VOLUME, 0);
    unsigned previous_gets = control_gets;
    tusb_control_request_t bad = {
        .bRequest = AUDIO_CS_REQ_CUR, .wIndex = 0x0200, .wValue = 0x01ff, .wLength = 1};
    uint8_t value[2] = {0};
    assert(!tud_audio_set_req_entity_cb(0, &bad, value));
    assert(!tud_audio_get_req_entity_cb(0, &bad));
    assert(control_gets == previous_gets);
    bad.wValue = 0x0200;
    assert(!tud_audio_set_req_entity_cb(0, &bad, value)); /* Bad volume length. */
    bad.wLength = 2;
    value[1] = 1;
    assert(!tud_audio_set_req_entity_cb(0, &bad, value)); /* Unsupported positive dB. */
    /* Consumer stalls: ring bound and fresh-data recovery, never slow stale draining. */
    feed(16000, -12000, -12000);
    assert(mosaico_usb_audio_stats().queued_frames == 7680);
    assert(ops->read(capture, output + 1, 960) == 960);
    assert(output[960] <= -11999 && output[960] >= -12001);
    assert(mosaico_usb_audio_stats().queued_frames == 960);
    assert(mosaico_usb_audio_stats().capture_dropped_frames > 0);
    interface(false);
    assert(!mosaico_usb_audio_stats().speaker_open);
    assert(ops->read(capture, output + 1, 960) == 960);
    silent(output + 1, 960);
    interface(true);
    feed(2880, 0, 0);
    assert(ops->read(capture, output + 1, 960) == 960);
    silent(output + 1, 960); /* No old filter tail crosses a stream lifetime. */
    feed(2880, 20000, 20000);
    suspended = true;
    event(TINYUSB_EVENT_SUSPENDED);
    assert(ops->read(capture, output + 1, 960) < 0);
    suspended = false;
    assert(ops->read(capture, output + 1, 960) == 960);
    silent(output + 1, 960);
    event(TINYUSB_EVENT_DETACHED);
    assert(!mosaico_usb_audio_stats().speaker_open && !mosaico_usb_audio_stats().queued_frames);
    assert(ops->stop(capture) == 0);
    assert(ops->read(capture, output + 1, 960) == 0);
    assert(ops->start(capture) < 0);
    ops->destroy(capture);
    assert(ops->init(&capture, 16000, 1, 16) == 0);
    interface(true);
    assert(ops->start(capture) == 0);
    feed(2880, 0, 0);
    assert(ops->read(capture, output + 1, 960) == 960);
    silent(output + 1, 960);
    assert(ops->read(capture, NULL, 960) < 0);
    assert(ops->read(capture, output, 961) < 0);
    ops->destroy(capture);
    puts(
        "PASS USB capture lifecycle, priming, cadence, bounded backlog, feedback and stream reset");
}
static int16_t concurrent_output[960];
static int concurrent_result;
static void *read_worker(void *context) {
    pause_next_unlock = true;
    concurrent_result = mosaico_usb_audio_capture_ops()->read(context, concurrent_output, 960);
    return NULL;
}
static void test_concurrency(void) {
    const mybot_audio_capture_ops_t *ops = mosaico_usb_audio_capture_ops();
    mosaico_usb_audio_prepare();
    mounted = true;
    interface(true);
    void *capture;
    assert(ops->init(&capture, 16000, 1, 16) == 0);
    for (unsigned close_stream = 0; close_stream != 2; ++close_stream) {
        assert(ops->start(capture) == 0);
        feed(2880, 18000, 18000);
        reader_paused = reader_released = false;
        pthread_t worker;
        assert(pthread_create(&worker, NULL, read_worker, capture) == 0);
        assert(pthread_mutex_lock(&barrier_lock) == 0);
        while (!reader_paused)
            assert(pthread_cond_wait(&barrier_cond, &barrier_lock) == 0);
        assert(pthread_mutex_unlock(&barrier_lock) == 0);
        if (close_stream)
            interface(false);
        else
            assert(ops->stop(capture) == 0); /* Returns while read is held, no join deadlock. */
        assert(pthread_mutex_lock(&barrier_lock) == 0);
        reader_released = true;
        assert(pthread_cond_signal(&barrier_cond) == 0);
        assert(pthread_mutex_unlock(&barrier_lock) == 0);
        assert(pthread_join(worker, NULL) == 0);
        assert(concurrent_result == (close_stream ? 960 : 0));
        silent(concurrent_output, 960);
        assert(mosaico_usb_audio_stats().queued_frames == 0);
    }
    ops->destroy(capture);
    puts(
        "PASS USB capture concurrent stop/stream-close invalidates in-flight PCM without blocking");
}
static int16_t dsp_input[48000 * 2], dsp_output[16000], fragmented_output[16000];
static double response(double frequency) {
    for (unsigned i = 0; i < 48000; ++i)
        dsp_input[i * 2] = dsp_input[i * 2 + 1] =
            (int16_t)(20000 * sin(6.283185307179586 * frequency * i / 48000));
    mosaico_usb_resampler_t state = {0};
    assert(mosaico_usb_resample(&state, dsp_input, 48000, dsp_output) == 16000);
    double energy = 0;
    for (unsigned i = 1000; i < 16000; ++i)
        energy += (double)dsp_output[i] * dsp_output[i];
    return sqrt(energy / 15000) / (20000 / sqrt(2));
}
static void test_resampler(void) {
    double pass = response(1000), alias = response(12000), edge = response(8500);
    assert(pass > 0.99 && pass < 1.01);
    assert(alias < 0.001 && edge < 0.001);
    mosaico_usb_resampler_t state = {0};
    unsigned output = 0;
    for (unsigned offset = 0; offset < 48000;) {
        unsigned amount = 7 + offset % 17;
        if (amount > 48000 - offset)
            amount = 48000 - offset;
        output += mosaico_usb_resample(&state, dsp_input + offset * 2, amount,
                                       fragmented_output + output);
        offset += amount;
    }
    assert(output == 16000 && !memcmp(dsp_output, fragmented_output, sizeof(dsp_output)));
    for (unsigned i = 0; i < 48000; ++i) {
        dsp_input[i * 2] = 32767;
        dsp_input[i * 2 + 1] = -32767;
    }
    mosaico_usb_resampler_reset(&state);
    assert(mosaico_usb_resample(&state, dsp_input, 48000, dsp_output) == 16000);
    silent(dsp_output, 16000);
    printf("PASS USB stereo FIR decimator unity/split equivalence, 1k=%.5f 8.5k=%.6f 12k=%.6f\n",
           pass, edge, alias);
}
static void microphone_interface(bool open) {
    tusb_control_request_t request = {.wIndex = MYBOT_USB_ITF_MICROPHONE, .wValue = open};
    assert(open ? tud_audio_set_itf_cb(0, &request) : tud_audio_set_itf_close_ep_cb(0, &request));
}
static unsigned host_microphone(unsigned packets, bool expect_silence) {
    unsigned peak = 0;
    for (unsigned p = 0; p < packets; ++p) {
        assert(__wrap_usbd_edpt_xfer_fifo(0, MYBOT_USB_EP_AUDIO_IN, NULL, 0));
        unsigned frames = speed == TUSB_SPEED_HIGH ? 6 : 48;
        assert(last_bytes == frames * 4);
        const int16_t *samples = (const int16_t *)last_buffer;
        for (unsigned i = 0; i < frames; ++i) {
            assert(samples[i * 2] == samples[i * 2 + 1]);
            if (expect_silence)
                assert(samples[i * 2] == 0);
            unsigned value = samples[i * 2] < 0 ? -(int)samples[i * 2] : samples[i * 2];
            if (value > peak)
                peak = value;
        }
    }
    return peak;
}
static int16_t spoken[960];
static void test_playback(void) {
    const mybot_audio_playback_ops_t *ops = mosaico_usb_audio_playback_ops();
    const mybot_audio_capture_ops_t *capture_ops = mosaico_usb_audio_capture_ops();
    void *playback, *duplicate, *capture;
    assert(ops->init(NULL, 16000, 1, 16) < 0);
    assert(ops->init(&playback, 48000, 1, 16) < 0);
    assert(ops->init(&playback, 16000, 1, 16) == 0);
    assert(ops->init(&duplicate, 16000, 1, 16) < 0 && !duplicate);
    assert(ops->start(playback) < 0);
    mosaico_usb_audio_prepare();
    mounted = true;
    /* USB microphone may be opened by the call app after translation starts. */
    assert(ops->start(playback) == 0);
    for (unsigned i = 0; i < 960; ++i)
        spoken[i] = 18000;
    assert(ops->write(playback, spoken, 960) == 960);
    assert(!mosaico_usb_audio_stats().playback_queued_frames);
    microphone_interface(true);
    host_microphone(300, true);
    assert(ops->write(playback, spoken, 960) == 960);
    host_microphone(150, true); /* At least 18.75 ms jitter prebuffer. */
    assert(host_microphone(330, false) >= 17990);
    assert(mosaico_usb_audio_stats().playback_queued_frames >= 950);
    assert(mosaico_usb_audio_stats().playback_queued_frames <= 970);
    /* Both directions run independently. Closing OUT must not clear SPEAK. */
    interface(true);
    assert(capture_ops->init(&capture, 16000, 1, 16) == 0);
    assert(capture_ops->start(capture) == 0);
    feed(2880, 2000, 2000);
    unsigned before = mosaico_usb_audio_stats().playback_queued_frames;
    interface(false);
    assert(mosaico_usb_audio_stats().playback_queued_frames == before);
    assert(host_microphone(10, false) >= 17990);
    interface(true);
    feed(2880, 2000, 2000);
    before = mosaico_usb_audio_stats().queued_frames;
    microphone_interface(false);
    assert(mosaico_usb_audio_stats().queued_frames == before);
    assert(!mosaico_usb_audio_stats().playback_queued_frames);
    assert(ops->write(playback, spoken, 960) == 960);
    microphone_interface(true);
    host_microphone(500, true);
    memset(spoken, 0, sizeof(spoken));
    assert(ops->write(playback, spoken, 960) == 960);
    host_microphone(650, true); /* Filter cannot retain a previous lifetime's tail. */
    for (unsigned i = 0; i < 960; ++i)
        spoken[i] = -12000;
    assert(ops->write(playback, spoken, 960) == 960);
    assert(host_microphone(480, false) >= 11990);
    /* Stop cannot retract a packet already handed to the controller; it must
     * not mutate that packet, and every subsequent packet must be silence. */
    int16_t in_flight[12];
    memcpy(in_flight, last_buffer, sizeof(in_flight));
    assert(ops->stop(playback) == 0);
    assert(!memcmp(in_flight, last_buffer, sizeof(in_flight)));
    host_microphone(600, true);
    assert(ops->write(playback, spoken, 960) == 0);
    assert(ops->start(playback) == 0);
    host_microphone(600, true);
    assert(ops->write(playback, spoken, 960) == 960);
    event(TINYUSB_EVENT_SUSPENDED);
    host_microphone(600, true);
    suspended = true;
    assert(ops->write(playback, spoken, 960) < 0);
    suspended = false;
    event(TINYUSB_EVENT_DETACHED);
    mounted = false;
    assert(ops->write(playback, spoken, 960) < 0);
    mounted = true;
    microphone_interface(true);
    host_microphone(600, true);
    ops->destroy(playback);
    assert(ops->init(&playback, 16000, 1, 16) == 0);
    assert(ops->start(playback) == 0);
    assert(ops->write(playback, spoken, 961) < 0);
    assert(ops->write(playback, NULL, 960) < 0);
    for (unsigned i = 0; i < 20; ++i)
        assert(ops->write(playback, spoken, 960) == 960);
    assert(mosaico_usb_audio_stats().playback_queued_frames == 7680);
    assert(mosaico_usb_audio_stats().playback_dropped_frames > 0);
    ops->destroy(playback);
    capture_ops->destroy(capture);
    host_microphone(300, true);
    puts("PASS USB translated return host clock, prebuffer, independent duplex, stop and no stale "
         "PCM");
}
static void *write_worker(void *context) {
    pause_next_unlock = true;
    concurrent_result = mosaico_usb_audio_playback_ops()->write(context, spoken, 960);
    return NULL;
}
static void test_playback_concurrency(void) {
    const mybot_audio_playback_ops_t *ops = mosaico_usb_audio_playback_ops();
    mosaico_usb_audio_prepare();
    mounted = true;
    void *playback;
    assert(ops->init(&playback, 16000, 1, 16) == 0);
    for (unsigned close_stream = 0; close_stream < 3; ++close_stream) {
        microphone_interface(true);
        assert(ops->start(playback) == 0);
        for (unsigned i = 0; i < 960; ++i)
            spoken[i] = 16000;
        reader_paused = reader_released = false;
        pthread_t worker;
        assert(pthread_create(&worker, NULL, write_worker, playback) == 0);
        assert(pthread_mutex_lock(&barrier_lock) == 0);
        while (!reader_paused)
            assert(pthread_cond_wait(&barrier_cond, &barrier_lock) == 0);
        assert(pthread_mutex_unlock(&barrier_lock) == 0);
        if (close_stream == 2)
            ops->destroy(playback);
        else if (close_stream)
            microphone_interface(false);
        else
            assert(ops->stop(playback) == 0);
        assert(pthread_mutex_lock(&barrier_lock) == 0);
        reader_released = true;
        assert(pthread_cond_signal(&barrier_cond) == 0);
        assert(pthread_mutex_unlock(&barrier_lock) == 0);
        assert(pthread_join(worker, NULL) == 0);
        assert(concurrent_result == (close_stream == 1 ? 960 : 0));
        assert(!mosaico_usb_audio_stats().playback_queued_frames);
        host_microphone(600, true);
    }
    assert(ops->init(&playback, 16000, 1, 16) == 0); /* In-flight destroy reclaimed itself. */
    ops->destroy(playback);
    puts("PASS USB return concurrent stop/close/destroy invalidate interpolation without blocking");
}
static void test_playback_drift(void) {
    const mybot_audio_playback_ops_t *ops = mosaico_usb_audio_playback_ops();
    mosaico_usb_audio_prepare();
    mounted = true;
    microphone_interface(true);
    void *playback;
    assert(ops->init(&playback, 16000, 1, 16) == 0 && ops->start(playback) == 0);
    for (unsigned i = 0; i < 960; ++i)
        spoken[i] = 10000;
    for (unsigned direction = 0; direction < 2; ++direction) {
        unsigned zeros = 0, drops = mosaico_usb_audio_stats().playback_dropped_frames;
        for (unsigned block = 0; block < 1200; ++block) {
            assert(ops->write(playback, spoken, 960) == 960);
            /* Clock offset +/-417 ppm relative to 60 ms RTC playback cadence. */
            unsigned packets = 480;
            if (block % 5 == 0)
                packets = direction ? 479 : 481;
            host_microphone(packets, false);
            mosaico_usb_audio_stats_t stats = mosaico_usb_audio_stats();
            if (block == 0)
                zeros = stats.playback_zero_frames;
            assert(stats.playback_zero_frames == zeros);
            assert(stats.playback_dropped_frames == drops);
            assert(stats.playback_queued_frames > 300 && stats.playback_queued_frames < 1800);
        }
    }
    ops->destroy(playback);
    puts("PASS USB return bounded adaptive resampling for +/-417 ppm USB/RTC clock mismatch");
}
static int16_t interpolated_output[48000], interpolated_fragmented[48000], mono_input[16000];
static double bin_amplitude(const int16_t *samples, double frequency) {
    double sine = 0, cosine = 0;
    for (unsigned i = 4800; i < 48000; ++i) {
        double angle = 6.283185307179586 * frequency * i / 48000;
        sine += samples[i] * sin(angle);
        cosine += samples[i] * cos(angle);
    }
    return 2 * sqrt(sine * sine + cosine * cosine) / 43200;
}
static void test_interpolator(void) {
    for (unsigned i = 0; i < 16000; ++i)
        mono_input[i] = (int16_t)(20000 * sin(6.283185307179586 * 1000 * i / 16000));
    mosaico_usb_resampler_t state = {0};
    assert(mosaico_usb_interpolate(&state, mono_input, 16000, interpolated_output) == 48000);
    double pass = bin_amplitude(interpolated_output, 1000) / 20000;
    double image = bin_amplitude(interpolated_output, 15000) / 20000;
    assert(pass > 0.99 && pass < 1.01 && image < 0.001);
    mosaico_usb_resampler_reset(&state);
    for (unsigned offset = 0; offset < 16000;) {
        unsigned amount = 1 + offset % 17;
        if (amount > 16000 - offset)
            amount = 16000 - offset;
        assert(mosaico_usb_interpolate(&state, mono_input + offset, amount,
                                       interpolated_fragmented + offset * 3) == amount * 3);
        offset += amount;
    }
    assert(!memcmp(interpolated_output, interpolated_fragmented, sizeof(interpolated_output)));
    for (unsigned i = 0; i < 16000; ++i)
        mono_input[i] = 32767;
    mosaico_usb_resampler_reset(&state);
    assert(mosaico_usb_interpolate(&state, mono_input, 16000, interpolated_output) == 48000);
    for (unsigned i = 480; i < 48000; ++i)
        assert(interpolated_output[i] >= 32760); /* Unity gain and saturated overshoot. */
    printf("PASS USB polyphase interpolator chunk identity, 1k=%.5f image15k=%.6f\n", pass, image);
}

static tusb_control_request_t entity_request(unsigned entity, unsigned channel, unsigned selector,
                                             unsigned request, unsigned length) {
    return (tusb_control_request_t){.bRequest = request,
                                    .wIndex = entity << 8,
                                    .wValue = (selector << 8) | channel,
                                    .wLength = length};
}

static void get_control(tusb_control_request_t request, const uint8_t *expected, unsigned length) {
    assert(tud_audio_get_req_entity_cb(0, &request));
    assert(control_length == length && !memcmp(control_reply, expected, length));
    assert(control_wire_length == (length < request.wLength ? length : request.wLength));
}

static void test_controls(void) {
    mosaico_usb_audio_prepare();
    const uint8_t sample_rate[] = {0x80, 0xbb, 0, 0};
    const uint8_t rate_range[] = {1, 0, 0x80, 0xbb, 0, 0, 0x80, 0xbb, 0, 0, 0, 0, 0, 0};
    const uint8_t volume_range[] = {1, 0, 0, 0xce, 0, 0, 0, 1};
    const uint8_t one[] = {1}, zero[] = {0, 0};
    tusb_control_request_t rate = entity_request(4, 0, AUDIO_CS_CTRL_SAM_FREQ, AUDIO_CS_REQ_CUR, 4);
    get_control(rate, sample_rate, sizeof(sample_rate));
    tusb_control_request_t range =
        entity_request(4, 0, AUDIO_CS_CTRL_SAM_FREQ, AUDIO_CS_REQ_RANGE, 2);
    for (unsigned i = 0; i < 3; ++i) {
        range.wLength = i == 0 ? 2 : i == 1 ? sizeof(rate_range) : 64;
        get_control(range, rate_range, sizeof(rate_range));
    }
    get_control(entity_request(4, 0, AUDIO_CS_CTRL_CLK_VALID, AUDIO_CS_REQ_CUR, 1), one, 1);
    uint8_t clock_data[] = {0x80, 0xbb, 0, 0};
    assert(tud_audio_set_req_entity_cb(0, &rate, clock_data));
    clock_data[0] = 0x44;
    clock_data[1] = 0xac; /* 44.1 kHz is not advertised or accepted. */
    assert(!tud_audio_set_req_entity_cb(0, &rate, clock_data));
    rate.wLength = 3;
    assert(!tud_audio_set_req_entity_cb(0, &rate, clock_data));
    get_control(entity_request(4, 0, AUDIO_CS_CTRL_SAM_FREQ, AUDIO_CS_REQ_CUR, 4), sample_rate, 4);

    const int volumes[] = {-25 * 256, -6 * 256 - 128, -50 * 256};
    for (unsigned channel = 0; channel < 3; ++channel) {
        get_control(entity_request(2, channel, AUDIO_FU_CTRL_MUTE, AUDIO_CS_REQ_CUR, 1), zero, 1);
        get_control(entity_request(2, channel, AUDIO_FU_CTRL_VOLUME, AUDIO_CS_REQ_CUR, 2), zero, 2);
        for (unsigned i = 0; i < 3; ++i)
            get_control(entity_request(2, channel, AUDIO_FU_CTRL_VOLUME, AUDIO_CS_REQ_RANGE,
                                       i == 0   ? 2
                                       : i == 1 ? 8
                                                : 64),
                        volume_range, sizeof(volume_range));
        control(channel, AUDIO_FU_CTRL_MUTE, 1);
        get_control(entity_request(2, channel, AUDIO_FU_CTRL_MUTE, AUDIO_CS_REQ_CUR, 1), one, 1);
        control(channel, AUDIO_FU_CTRL_MUTE, 0);
        control(channel, AUDIO_FU_CTRL_VOLUME, volumes[channel]);
        uint8_t expected[] = {(uint8_t)volumes[channel], (uint8_t)(volumes[channel] >> 8)};
        get_control(entity_request(2, channel, AUDIO_FU_CTRL_VOLUME, AUDIO_CS_REQ_CUR, 2), expected,
                    2);
    }
    assert(mosaico_usb_audio_stats().volume == 50 && !mosaico_usb_audio_stats().muted);
    mosaico_usb_audio_prepare(); /* Repeated preparation cannot erase host controls. */
    const uint8_t master_volume[] = {0, 0xe7};
    get_control(entity_request(2, 0, AUDIO_FU_CTRL_VOLUME, AUDIO_CS_REQ_CUR, 2), master_volume, 2);

    tusb_control_request_t bad[] = {
        /* Neither fixed input terminal advertises Connector Control (selector 0x02). */
        entity_request(0x01, 0, 0x02, AUDIO_CS_REQ_CUR, 6),
        entity_request(0x11, 0, 0x02, AUDIO_CS_REQ_CUR, 6),
        /* The production microphone path has no Feature Unit controls. */
        entity_request(0x12, 0, AUDIO_FU_CTRL_MUTE, AUDIO_CS_REQ_CUR, 1),
        entity_request(0x12, 0, AUDIO_FU_CTRL_VOLUME, AUDIO_CS_REQ_CUR, 2),
        entity_request(99, 0, AUDIO_FU_CTRL_MUTE, AUDIO_CS_REQ_CUR, 1),
        entity_request(2, 3, AUDIO_FU_CTRL_MUTE, AUDIO_CS_REQ_CUR, 1),
        entity_request(2, 0, 99, AUDIO_CS_REQ_CUR, 1),
        entity_request(2, 0, AUDIO_FU_CTRL_MUTE, AUDIO_CS_REQ_RANGE, 1),
        entity_request(4, 1, AUDIO_CS_CTRL_SAM_FREQ, AUDIO_CS_REQ_CUR, 4),
        entity_request(4, 0, AUDIO_CS_CTRL_CLK_VALID, AUDIO_CS_REQ_RANGE, 1),
    };
    uint8_t data[6] = {0};
    unsigned gets_before = control_gets;
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        assert(!tud_audio_get_req_entity_cb(0, &bad[i]));
        assert(!tud_audio_set_req_entity_cb(0, &bad[i], data));
    }
    assert(control_gets == gets_before);
    get_control(entity_request(4, 0, AUDIO_CS_CTRL_SAM_FREQ, AUDIO_CS_REQ_CUR, 4), sample_rate, 4);
    for (unsigned channel = 0; channel < 3; ++channel) {
        get_control(entity_request(2, channel, AUDIO_FU_CTRL_MUTE, AUDIO_CS_REQ_CUR, 1), zero, 1);
        uint8_t expected[] = {(uint8_t)volumes[channel], (uint8_t)(volumes[channel] >> 8)};
        get_control(entity_request(2, channel, AUDIO_FU_CTRL_VOLUME, AUDIO_CS_REQ_CUR, 2), expected,
                    2);
    }
    tusb_control_request_t volume = entity_request(2, 0, AUDIO_FU_CTRL_VOLUME, AUDIO_CS_REQ_CUR, 2);
    assert(!tud_audio_get_req_entity_cb(1, &volume));
    assert(!tud_audio_set_req_entity_cb(1, &volume, data));
    assert(!tud_audio_get_req_entity_cb(0, NULL));
    assert(!tud_audio_set_req_entity_cb(0, NULL, data));
    assert(!tud_audio_set_req_entity_cb(0, &volume, NULL));
    tusb_control_request_t wrong_interface = volume;
    ++wrong_interface.wIndex;
    assert(!tud_audio_get_req_entity_cb(0, &wrong_interface));
    assert(!tud_audio_set_req_entity_cb(0, &wrong_interface, data));
    volume.wLength = 1;
    assert(!tud_audio_set_req_entity_cb(0, &volume, data));
    volume.wLength = 2;
    data[0] = 1;
    assert(!tud_audio_set_req_entity_cb(0, &volume, data)); /* Positive volume. */
    data[0] = 0xff;
    data[1] = 0xcd;
    assert(!tud_audio_set_req_entity_cb(0, &volume, data)); /* Below -50 dB. */
    tusb_control_request_t mute = entity_request(2, 0, AUDIO_FU_CTRL_MUTE, AUDIO_CS_REQ_CUR, 1);
    data[0] = 2;
    assert(!tud_audio_set_req_entity_cb(0, &mute, data));
    get_control(volume, master_volume, 2);
    control_result = false;
    assert(!tud_audio_get_req_entity_cb(0, &volume));
    control_result = true;

    for (unsigned itf = MYBOT_USB_ITF_SPEAKER; itf <= MYBOT_USB_ITF_MICROPHONE; ++itf) {
        tusb_control_request_t request = {.wIndex = itf, .wValue = 1};
        assert(tud_audio_set_itf_cb(0, &request));
        assert(tud_audio_set_itf_close_ep_cb(0, &request)); /* Same-alt reopen passes new alt. */
        assert(tud_audio_set_itf_cb(0, &request));
        assert(itf == MYBOT_USB_ITF_SPEAKER ? mosaico_usb_audio_stats().speaker_open
                                            : mosaico_usb_audio_stats().microphone_open);
        request.wValue = 2;
        assert(!tud_audio_set_itf_cb(0, &request));
        request.wValue = 0;
        assert(!tud_audio_set_itf_cb(1, &request));
        assert(tud_audio_set_itf_close_ep_cb(0, &request));
    }
    assert(!mosaico_usb_audio_stats().speaker_open && !mosaico_usb_audio_stats().microphone_open);
    tusb_control_request_t not_audio = {.wIndex = MYBOT_USB_ITF_CDC};
    assert(!tud_audio_set_itf_cb(0, &not_audio) && !tud_audio_set_itf_cb(0, NULL));
    puts("PASS project UAC clock and channel controls, RANGE probes, validation and interface "
         "lifetime");
}
int main(int argc, char **argv) {
    assert(argc == 2);
    if (!strcmp(argv[1], "normal")) {
        test_transport();
    } else if (!strcmp(argv[1], "controls")) {
        test_controls();
    } else if (!strcmp(argv[1], "capture")) {
        test_capture();
    } else if (!strcmp(argv[1], "concurrency")) {
        test_concurrency();
    } else if (!strcmp(argv[1], "playback")) {
        test_playback();
    } else if (!strcmp(argv[1], "playback_concurrency")) {
        test_playback_concurrency();
    } else if (!strcmp(argv[1], "playback_drift")) {
        test_playback_drift();
    } else if (!strcmp(argv[1], "interpolator")) {
        test_interpolator();
    } else if (!strcmp(argv[1], "resampler")) {
        test_resampler();
    } else {
        assert(false);
    }
}
