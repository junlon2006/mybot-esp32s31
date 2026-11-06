/* SPDX-FileCopyrightText: 2026 MyBot Project Contributors */
/* SPDX-License-Identifier: Apache-2.0 */
#include "usb/mosaico_usb_audio.h"
#include "usb/mosaico_usb_descriptors.h"
#include "usb/mosaico_usb_resampler.h"

#include <stdatomic.h>
#include <stddef.h>
#include <math.h>
#include <string.h>
#include "device/dcd.h"
#include "device/usbd_pvt.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "tusb.h"
#include "usb_device_uac.h"
#include "uac_descriptors.h"

#define TAG "mosaico_uac"
#define USB_AUDIO_RATE 48000U
#define USB_AUDIO_FRAME_BYTES 4U
#define USB_RING_FRAMES 7680U /* 160 ms hard limit; normal retained audio is 20 ms. */
#define USB_RESERVE_FRAMES 960U
#define USB_COPY_FRAMES 48U
#define CAPTURE_MAX_FRAMES 960U
#define USB_RATE_Q16 (USB_AUDIO_RATE << 16)

_Static_assert(ATOMIC_INT_LOCK_FREE == 2, "USB ISR counters must be lock-free");
_Static_assert(CONFIG_USB_DEVICE_UAC_AS_PART, "Board owns the only USB stack");
_Static_assert(CONFIG_UAC_SAMPLE_RATE == USB_AUDIO_RATE && CONFIG_UAC_SPEAKER_CHANNEL_NUM == 2 &&
                   CONFIG_UAC_MIC_CHANNEL_NUM == 2 && CONFIG_UAC_BIT_RESOLUTION == 16,
               "Transport format is 48 kHz stereo S16 PCM");
_Static_assert(TUD_AUDIO_PREFER_RING_BUFFER && !CFG_TUD_DWC2_DMA_ENABLE &&
                   !CFG_TUD_MEM_DCACHE_ENABLE,
               "Keep the validated S31 DWC2 PIO audio path");

static bool s_attempted;
static esp_err_t s_result = ESP_ERR_INVALID_STATE;
static atomic_bool s_prepared;
static atomic_bool s_muted;
static atomic_uint s_volume = 100;
static atomic_uint s_mounts, s_unmounts, s_suspends, s_resumes;
static atomic_uint s_rx_packets, s_rx_bytes, s_rx_zero_packets, s_out_errors;
static atomic_uint s_output_callbacks, s_discarded_bytes, s_output_errors;
static atomic_uint s_input_callbacks, s_silence_bytes, s_input_errors;
static atomic_uint s_mic_completed, s_mic_bytes, s_mic_errors;
static atomic_uint s_fb_attempts, s_fb_submitted, s_fb_submit_errors;
static atomic_uint s_fb_completed, s_fb_bytes, s_fb_errors, s_fb_value;

static void count(atomic_uint *counter, unsigned value) {
    atomic_fetch_add_explicit(counter, value, memory_order_relaxed);
}

/* ISR-visible raw PCM must stay internal. Only short bounded copies hold this
 * lock; FIR work and USB FIFO operations never do. No heap object is shared
 * with the ISR, so capture destroy cannot leave a stale interrupt pointer. */
static DRAM_ATTR int16_t s_ring[USB_RING_FRAMES * 2];
static portMUX_TYPE s_capture_lock = portMUX_INITIALIZER_UNLOCKED;
static struct {
    bool claimed, started, reading, speaker_open, microphone_open;
    unsigned head, count, generation;
    bool priming;
} s_capture;
static struct {
    unsigned generation;
    mosaico_usb_resampler_t filter;
} s_reader;
static atomic_uint s_capture_frames, s_capture_zero_frames, s_capture_dropped_frames;
static atomic_uint s_capture_peak;
static atomic_uint s_feedback_rate_q16 = USB_RATE_Q16;
/* Channel 0 is master, 1/2 are L/R. Host USB volume controls this digital input;
 * it is independent of the device's translated-audio speaker volume. */
static atomic_uint s_channel_gain[3] = {32768, 32768, 32768};
static atomic_bool s_channel_mute[3];

static void clear_capture_locked(void) {
    s_capture.head = s_capture.count = 0;
    ++s_capture.generation;
    s_capture.priming = true;
    atomic_store_explicit(&s_feedback_rate_q16, USB_RATE_Q16, memory_order_relaxed);
}

static int capture_init(void **context, int rate, int channels, int bits) {
    if (!context || rate != 16000 || channels != 1 || bits != 16)
        return -1;
    *context = NULL;
    portENTER_CRITICAL_ISR(&s_capture_lock);
    bool available = !s_capture.claimed;
    if (available) {
        s_capture.claimed = true;
        s_capture.started = false;
        clear_capture_locked();
        *context = &s_capture;
    }
    portEXIT_CRITICAL_ISR(&s_capture_lock);
    return available ? 0 : -1;
}

static int capture_start(void *context) {
    if (context != &s_capture)
        return -1;
    bool ready = atomic_load_explicit(&s_prepared, memory_order_acquire) && tud_mounted() &&
                 !tud_suspended();
    portENTER_CRITICAL_ISR(&s_capture_lock);
    bool valid = s_capture.claimed && ready && s_capture.speaker_open && !s_capture.reading;
    if (valid && !s_capture.started) {
        clear_capture_locked();
        s_capture.started = true;
    }
    portEXIT_CRITICAL_ISR(&s_capture_lock);
    return valid ? 0 : -1;
}

static int capture_stop(void *context) {
    if (context != &s_capture)
        return -1;
    portENTER_CRITICAL_ISR(&s_capture_lock);
    bool valid = s_capture.claimed;
    s_capture.started = false;
    clear_capture_locked();
    portEXIT_CRITICAL_ISR(&s_capture_lock);
    return valid ? 0 : -1;
}

static void capture_destroy(void *context) {
    if (context != &s_capture)
        return;
    capture_stop(context);
    /* SDK joins its capture worker before destroy. Storage remains driver-owned
     * even on misuse, never freeing memory which an in-flight read can use. */
    portENTER_CRITICAL_ISR(&s_capture_lock);
    if (!s_capture.reading)
        s_capture.claimed = false;
    portEXIT_CRITICAL_ISR(&s_capture_lock);
}

static int capture_read(void *context, void *buffer, int frames) {
    if (context != &s_capture || !buffer || frames <= 0 || (unsigned)frames > CAPTURE_MAX_FRAMES)
        return -1;
    if (!tud_mounted() || tud_suspended())
        return -1;
    int16_t *output = buffer;
    unsigned requested = (unsigned)frames * 3;
    unsigned generation, consume, prefix = 0;
    portENTER_CRITICAL_ISR(&s_capture_lock);
    if (!s_capture.claimed || !s_capture.started || s_capture.reading) {
        portEXIT_CRITICAL_ISR(&s_capture_lock);
        return 0;
    }
    s_capture.reading = true;
    /* Preserve headroom for clock feedback: ordinary drift must not be trimmed
     * at the target itself. A stalled consumer exceeding 60 ms retained audio
     * catches up to this read plus the normal 20 ms reserve. */
    unsigned limit = requested + USB_RESERVE_FRAMES;
    if (s_capture.count > requested + 3 * USB_RESERVE_FRAMES) {
        unsigned dropped = s_capture.count - limit;
        s_capture.head = (s_capture.head + dropped) % USB_RING_FRAMES;
        s_capture.count -= dropped;
        ++s_capture.generation;
        count(&s_capture_dropped_frames, dropped);
    }
    if (s_capture.priming) {
        unsigned reserve =
            s_capture.count < USB_RESERVE_FRAMES ? s_capture.count : USB_RESERVE_FRAMES;
        consume = s_capture.count - reserve;
        if (consume > requested)
            consume = requested;
        consume -= consume % 3;
        prefix = (requested - consume) / 3;
        if (consume)
            s_capture.priming = false;
    } else {
        consume = s_capture.count < requested ? s_capture.count : requested;
        consume -= consume % 3;
    }
    generation = s_capture.generation;
    portEXIT_CRITICAL_ISR(&s_capture_lock);

    memset(output, 0, (size_t)frames * sizeof(*output));
    if (s_reader.generation != generation) {
        mosaico_usb_resampler_reset(&s_reader.filter);
        s_reader.generation = generation;
    }
    uint32_t gain[2];
    uint32_t master = atomic_load_explicit(&s_channel_mute[0], memory_order_relaxed)
                          ? 0
                          : atomic_load_explicit(&s_channel_gain[0], memory_order_relaxed);
    for (unsigned channel = 0; channel < 2; ++channel) {
        uint32_t individual =
            atomic_load_explicit(&s_channel_mute[channel + 1], memory_order_relaxed)
                ? 0
                : atomic_load_explicit(&s_channel_gain[channel + 1], memory_order_relaxed);
        gain[channel] = master * individual / 32768;
    }
    unsigned produced = 0;
    while (consume) {
        int16_t chunk[USB_COPY_FRAMES * 2];
        unsigned amount = consume < USB_COPY_FRAMES ? consume : USB_COPY_FRAMES;
        portENTER_CRITICAL_ISR(&s_capture_lock);
        bool valid = s_capture.started && s_capture.generation == generation;
        if (valid) {
            unsigned first = USB_RING_FRAMES - s_capture.head;
            if (first > amount)
                first = amount;
            memcpy(chunk, &s_ring[s_capture.head * 2], first * USB_AUDIO_FRAME_BYTES);
            if (first < amount)
                memcpy(chunk + first * 2, s_ring, (amount - first) * USB_AUDIO_FRAME_BYTES);
            s_capture.head = (s_capture.head + amount) % USB_RING_FRAMES;
            s_capture.count -= amount;
        }
        portEXIT_CRITICAL_ISR(&s_capture_lock);
        if (!valid)
            break;
        for (unsigned i = 0; i < amount * 2; ++i)
            chunk[i] = (int16_t)((int32_t)chunk[i] * (int32_t)gain[i % 2] / 32768);
        produced +=
            mosaico_usb_resample(&s_reader.filter, chunk, amount, output + prefix + produced);
        consume -= amount;
    }
    if (prefix + produced < (unsigned)frames)
        mosaico_usb_resampler_reset(&s_reader.filter);
    unsigned peak = 0;
    for (int i = 0; i < frames; ++i) {
        unsigned value = output[i] < 0 ? -(int)output[i] : output[i];
        if (value > peak)
            peak = value;
    }
    /* A close/stop between copy and FIR must also invalidate copied PCM. */
    portENTER_CRITICAL_ISR(&s_capture_lock);
    bool running = s_capture.started;
    bool current = running && generation == s_capture.generation;
    if (current && s_capture.speaker_open && !s_capture.priming) {
        /* Read at the SDK's 60 ms cadence. Sampling the residual once per read
         * avoids reacting to the normal 60 ms queue sawtooth in every USB ISR.
         * Proportional correction is bounded to 0.2% and converges gradually. */
        int error = (int)USB_RESERVE_FRAMES - (int)s_capture.count;
        int32_t correction = error * 3277; /* 0.05 Hz per frame, Q16 */
        const int32_t bound = 96 * 65536;
        if (correction > bound)
            correction = bound;
        if (correction < -bound)
            correction = -bound;
        atomic_store_explicit(&s_feedback_rate_q16, USB_RATE_Q16 + correction,
                              memory_order_relaxed);
    }
    if (!current)
        mosaico_usb_resampler_reset(&s_reader.filter);
    /* Finish all shared reader state before permitting a subsequent start/read. */
    atomic_store_explicit(&s_capture_peak, current ? peak : 0, memory_order_relaxed);
    if (running) {
        count(&s_capture_frames, current ? produced : 0);
        count(&s_capture_zero_frames, (unsigned)frames - (current ? produced : 0));
    }
    s_capture.reading = false;
    portEXIT_CRITICAL_ISR(&s_capture_lock);
    if (!current)
        memset(output, 0, (size_t)frames * sizeof(*output));
    if (!running)
        return 0;
    return frames;
}

const mybot_audio_capture_ops_t *mosaico_usb_audio_capture_ops(void) {
    static const mybot_audio_capture_ops_t ops = {
        .init = capture_init,
        .start = capture_start,
        .read = capture_read,
        .stop = capture_stop,
        .destroy = capture_destroy,
    };
    return &ops;
}

/* The SPEAK sink owns a separate, internal mono ring. Only the SDK playback
 * worker runs interpolation; the USB ISR copies at most 48 frames. Its packet
 * is immutable between submission and completion, including during stop. */
static DRAM_ATTR int16_t s_playback_ring[USB_RING_FRAMES];
static DRAM_ATTR int16_t s_microphone_packet[48 * 2];
static portMUX_TYPE s_playback_lock = portMUX_INITIALIZER_UNLOCKED;
static struct {
    bool claimed, started, writing, microphone_open, priming, release_pending;
    unsigned head, count, generation, prime_frames;
} s_playback;
static struct {
    mosaico_usb_resampler_t filter;
    unsigned generation;
    int32_t position;
    int16_t previous;
} s_writer;
static atomic_uint s_playback_frames, s_playback_zero_frames;
static atomic_uint s_playback_dropped_frames, s_playback_peak;

static void clear_playback_locked(void) {
    s_playback.head = s_playback.count = 0;
    ++s_playback.generation;
    s_playback.priming = true;
    s_playback.prime_frames = USB_RESERVE_FRAMES;
    atomic_store_explicit(&s_playback_peak, 0, memory_order_relaxed);
}

static int playback_init(void **context, int rate, int channels, int bits) {
    if (!context || rate != 16000 || channels != 1 || bits != 16)
        return -1;
    *context = NULL;
    portENTER_CRITICAL_ISR(&s_playback_lock);
    bool available = !s_playback.claimed;
    if (available) {
        s_playback.claimed = true;
        s_playback.started = s_playback.release_pending = false;
        clear_playback_locked();
        *context = &s_playback;
    }
    portEXIT_CRITICAL_ISR(&s_playback_lock);
    return available ? 0 : -1;
}

static int playback_start(void *context) {
    if (context != &s_playback)
        return -1;
    bool ready = atomic_load_explicit(&s_prepared, memory_order_acquire) && tud_mounted() &&
                 !tud_suspended();
    portENTER_CRITICAL_ISR(&s_playback_lock);
    bool valid = s_playback.claimed && ready && !s_playback.writing;
    if (valid && !s_playback.started) {
        clear_playback_locked();
        s_playback.started = true;
    }
    portEXIT_CRITICAL_ISR(&s_playback_lock);
    return valid ? 0 : -1;
}

static int playback_stop(void *context) {
    if (context != &s_playback)
        return -1;
    portENTER_CRITICAL_ISR(&s_playback_lock);
    bool valid = s_playback.claimed;
    s_playback.started = false;
    clear_playback_locked();
    portEXIT_CRITICAL_ISR(&s_playback_lock);
    return valid ? 0 : -1;
}

static void playback_destroy(void *context) {
    if (context != &s_playback)
        return;
    playback_stop(context);
    portENTER_CRITICAL_ISR(&s_playback_lock);
    s_playback.release_pending = s_playback.writing;
    if (!s_playback.writing)
        s_playback.claimed = false;
    portEXIT_CRITICAL_ISR(&s_playback_lock);
}

static int playback_write(void *context, const void *buffer, int frames) {
    if (context != &s_playback || !buffer || frames <= 0 || (unsigned)frames > CAPTURE_MAX_FRAMES)
        return -1;
    if (!tud_mounted() || tud_suspended())
        return -1;
    portENTER_CRITICAL_ISR(&s_playback_lock);
    if (!s_playback.claimed || !s_playback.started || s_playback.writing) {
        portEXIT_CRITICAL_ISR(&s_playback_lock);
        return 0;
    }
    if (!s_playback.microphone_open) {
        count(&s_playback_dropped_frames, (unsigned)frames * 3);
        portEXIT_CRITICAL_ISR(&s_playback_lock);
        return frames;
    }
    s_playback.writing = true;
    unsigned generation = s_playback.generation;
    /* Sample the residual before each 60 ms write, not the burst's sawtooth.
     * Fine asynchronous resampling corrects sustained drift within +/-0.2%.
     * Positive queue error consumes the interpolated stream slightly faster.
     * Discontinuities remain bounded by the ring and re-prime with silence. */
    int correction =
        s_playback.priming ? 0 : ((int)s_playback.count - (int)USB_RESERVE_FRAMES) / 15;
    if (correction > 131)
        correction = 131;
    if (correction < -131)
        correction = -131;
    portEXIT_CRITICAL_ISR(&s_playback_lock);
    if (s_writer.generation != generation) {
        mosaico_usb_resampler_reset(&s_writer.filter);
        s_writer.generation = generation;
        s_writer.previous = 0;
        s_writer.position = 65536;
    }
    const int16_t *input = buffer;
    unsigned consumed = 0;
    while (consumed < (unsigned)frames) {
        int16_t interpolated[48], adjusted[50];
        unsigned amount = (unsigned)frames - consumed;
        if (amount > 16)
            amount = 16;
        unsigned produced =
            mosaico_usb_interpolate(&s_writer.filter, input + consumed, amount, interpolated);
        unsigned adjusted_count = 0;
        for (unsigned i = 0; i < produced; ++i) {
            /* The FIR already removed imaging. Linear interpolation here is
             * only the sub-sample correction between adjacent 48 kHz samples. */
            while (s_writer.position <= 65536) {
                int32_t delta = (int32_t)interpolated[i] - s_writer.previous;
                adjusted[adjusted_count++] =
                    s_writer.previous + (int32_t)((int64_t)delta * s_writer.position / 65536);
                s_writer.position += 65536 + correction;
            }
            s_writer.position -= 65536;
            s_writer.previous = interpolated[i];
        }
        portENTER_CRITICAL_ISR(&s_playback_lock);
        bool valid =
            s_playback.started && s_playback.microphone_open && s_playback.generation == generation;
        if (valid) {
            if (s_playback.count + adjusted_count > USB_RING_FRAMES) {
                unsigned dropped = s_playback.count + adjusted_count - USB_RING_FRAMES;
                s_playback.head = (s_playback.head + dropped) % USB_RING_FRAMES;
                s_playback.count -= dropped;
                count(&s_playback_dropped_frames, dropped);
            }
            unsigned tail = (s_playback.head + s_playback.count) % USB_RING_FRAMES;
            unsigned first = USB_RING_FRAMES - tail;
            if (first > adjusted_count)
                first = adjusted_count;
            memcpy(s_playback_ring + tail, adjusted, first * sizeof(int16_t));
            if (first < adjusted_count)
                memcpy(s_playback_ring, adjusted + first,
                       (adjusted_count - first) * sizeof(int16_t));
            s_playback.count += adjusted_count;
        }
        portEXIT_CRITICAL_ISR(&s_playback_lock);
        if (!valid)
            break;
        consumed += amount;
    }
    portENTER_CRITICAL_ISR(&s_playback_lock);
    bool running = s_playback.started;
    bool current = running && generation == s_playback.generation;
    s_playback.writing = false;
    if (s_playback.release_pending) {
        s_playback.claimed = false;
        s_playback.release_pending = false;
    }
    portEXIT_CRITICAL_ISR(&s_playback_lock);
    if (!running)
        return 0;
    if (!current)
        count(&s_playback_dropped_frames, ((unsigned)frames - consumed) * 3);
    return frames;
}

const mybot_audio_playback_ops_t *mosaico_usb_audio_playback_ops(void) {
    static const mybot_audio_playback_ops_t ops = {
        .init = playback_init,
        .start = playback_start,
        .write = playback_write,
        .stop = playback_stop,
        .destroy = playback_destroy,
    };
    return &ops;
}

esp_err_t mosaico_usb_audio_prepare(void) {
    if (s_attempted)
        return s_result;
    s_attempted = true;
    uac_device_config_t config = {
        .skip_tinyusb_init = true,
        .output_cb = NULL, /* Direct FIFO owner below bypasses the shared speaker buffer. */
        .input_cb = NULL,  /* Host clock owns microphone packets, no tick-driven shared buffers. */
        /* The channel-aware control wrapper owns mute/gain and master stats. */
        .set_mute_cb = NULL,
        .set_volume_cb = NULL,
        .spk_itf_num = MYBOT_USB_ITF_SPEAKER,
        .mic_itf_num = MYBOT_USB_ITF_MICROPHONE,
    };
    s_result = uac_device_init(&config);
    atomic_store_explicit(&s_prepared, s_result == ESP_OK, memory_order_release);
    return s_result;
}

void mosaico_usb_audio_event(tinyusb_event_t *event, void *arg) {
    (void)arg;
    if (!event || event->rhport != 0)
        return;
    if (event->id == TINYUSB_EVENT_DETACHED || event->id == TINYUSB_EVENT_SUSPENDED) {
        portENTER_CRITICAL_ISR(&s_capture_lock);
        if (event->id == TINYUSB_EVENT_DETACHED)
            s_capture.speaker_open = s_capture.microphone_open = false;
        clear_capture_locked();
        portEXIT_CRITICAL_ISR(&s_capture_lock);
        portENTER_CRITICAL_ISR(&s_playback_lock);
        if (event->id == TINYUSB_EVENT_DETACHED)
            s_playback.microphone_open = false;
        clear_playback_locked();
        portEXIT_CRITICAL_ISR(&s_playback_lock);
    }
    switch (event->id) {
    case TINYUSB_EVENT_ATTACHED:
        count(&s_mounts, 1);
        break;
    case TINYUSB_EVENT_DETACHED:
        count(&s_unmounts, 1);
        break;
#ifdef CONFIG_TINYUSB_SUSPEND_CALLBACK
    case TINYUSB_EVENT_SUSPENDED:
        count(&s_suspends, 1);
        break;
#endif
#ifdef CONFIG_TINYUSB_RESUME_CALLBACK
    case TINYUSB_EVENT_RESUMED:
        count(&s_resumes, 1);
        break;
#endif
    default:
        break;
    }
}

/* Rename only these package callbacks in the scoped compatibility header.
 * Own both PCM paths; the package's reusable buffers can otherwise be
 * overwritten while a task reads or replay old microphone data after stop. */
extern bool mosaico_uac_upstream_set_req(uint8_t, tusb_control_request_t const *, uint8_t *);
extern bool mosaico_uac_upstream_get_req(uint8_t, tusb_control_request_t const *);

static bool valid_speaker_control(tusb_control_request_t const *request) {
    return request && ((tu_le16toh(request->wIndex) >> 8) != UAC2_ENTITY_SPK_FEATURE_UNIT ||
                       (tu_le16toh(request->wValue) & 255) <= 2);
}
bool tud_audio_get_req_entity_cb(uint8_t rhport, tusb_control_request_t const *request) {
    return valid_speaker_control(request) && mosaico_uac_upstream_get_req(rhport, request);
}
bool tud_audio_set_req_entity_cb(uint8_t rhport, tusb_control_request_t const *request,
                                 uint8_t *data) {
    if (!valid_speaker_control(request) || !data)
        return false;
    bool speaker = (tu_le16toh(request->wIndex) >> 8) == UAC2_ENTITY_SPK_FEATURE_UNIT;
    unsigned selector = tu_le16toh(request->wValue) >> 8;
    unsigned channel = tu_le16toh(request->wValue) & 255;
    int16_t volume_db = 0;
    if (speaker) {
        if (request->bRequest != AUDIO_CS_REQ_CUR)
            return false;
        if (selector == AUDIO_FU_CTRL_MUTE) {
            if (tu_le16toh(request->wLength) != 1 || data[0] > 1)
                return false;
        } else if (selector == AUDIO_FU_CTRL_VOLUME) {
            if (tu_le16toh(request->wLength) != 2)
                return false;
            volume_db = (int16_t)((uint16_t)data[0] | (uint16_t)data[1] << 8);
            if (volume_db < -50 * 256 || volume_db > 0)
                return false;
        } else {
            return false;
        }
    }
    if (!mosaico_uac_upstream_set_req(rhport, request, data))
        return false;
    if (speaker && selector == AUDIO_FU_CTRL_MUTE) {
        atomic_store_explicit(&s_channel_mute[channel], data[0] != 0, memory_order_relaxed);
        if (channel == 0)
            atomic_store_explicit(&s_muted, data[0] != 0, memory_order_relaxed);
        portENTER_CRITICAL_ISR(&s_capture_lock);
        clear_capture_locked(); /* No pre-mute queue or FIR tail may cross this transition. */
        portEXIT_CRITICAL_ISR(&s_capture_lock);
    } else if (speaker && selector == AUDIO_FU_CTRL_VOLUME) {
        uint32_t gain = (uint32_t)lroundf(32768.0f * powf(10.0f, (float)volume_db / 5120.0f));
        atomic_store_explicit(&s_channel_gain[channel], gain, memory_order_relaxed);
        if (channel == 0)
            atomic_store_explicit(&s_volume, (volume_db + 50 * 256) * 100 / (50 * 256),
                                  memory_order_relaxed);
    }
    return true;
}

extern bool mosaico_uac_upstream_set_itf(uint8_t, tusb_control_request_t const *);
extern bool mosaico_uac_upstream_close_itf(uint8_t, tusb_control_request_t const *);
static void set_interface(tusb_control_request_t const *request) {
    unsigned interface = tu_le16toh(request->wIndex) & 255;
    bool open = (tu_le16toh(request->wValue) & 255) != 0;
    portENTER_CRITICAL_ISR(&s_capture_lock);
    if (interface == MYBOT_USB_ITF_SPEAKER) {
        s_capture.speaker_open = open;
        clear_capture_locked();
    } else if (interface == MYBOT_USB_ITF_MICROPHONE) {
        s_capture.microphone_open = open;
    }
    portEXIT_CRITICAL_ISR(&s_capture_lock);
    if (interface == MYBOT_USB_ITF_MICROPHONE) {
        portENTER_CRITICAL_ISR(&s_playback_lock);
        s_playback.microphone_open = open;
        clear_playback_locked();
        portEXIT_CRITICAL_ISR(&s_playback_lock);
    }
}
bool tud_audio_set_itf_cb(uint8_t rhport, tusb_control_request_t const *request) {
    /* The package's tick-driven microphone task stays asleep permanently. */
    bool microphone =
        rhport == 0 && request && (tu_le16toh(request->wIndex) & 255) == MYBOT_USB_ITF_MICROPHONE;
    bool result = microphone || mosaico_uac_upstream_set_itf(rhport, request);
    if (result && rhport == 0 && request)
        set_interface(request);
    return result;
}
bool tud_audio_set_itf_close_ep_cb(uint8_t rhport, tusb_control_request_t const *request) {
    bool microphone =
        rhport == 0 && request && (tu_le16toh(request->wIndex) & 255) == MYBOT_USB_ITF_MICROPHONE;
    bool result = microphone || mosaico_uac_upstream_close_itf(rhport, request);
    if (result && rhport == 0 && request)
        set_interface(request);
    return result;
}

bool tud_audio_rx_done_isr(uint8_t rhport, uint16_t bytes, uint8_t function, uint8_t endpoint,
                           uint8_t alternate) {
    (void)bytes;
    (void)alternate;
    if (rhport != 0 || function != 0 || endpoint != MYBOT_USB_EP_AUDIO_OUT)
        return false;
    count(&s_output_callbacks, 1);
    /* Bound work to the FIFO contents present at callback entry. Endpoint FIFO
     * ownership stays entirely on USB's callback; the SDK sees only s_ring. */
    unsigned remaining = tud_audio_n_available(function);
    if (remaining > CFG_TUD_AUDIO_FUNC_1_EP_OUT_SW_BUF_SZ || remaining % USB_AUDIO_FRAME_BYTES) {
        tud_audio_n_clear_ep_out_ff(function);
        count(&s_output_errors, 1);
        return true;
    }
    while (remaining) {
        int16_t chunk[USB_COPY_FRAMES * 2];
        unsigned requested = remaining < sizeof(chunk) ? remaining : sizeof(chunk);
        unsigned received = tud_audio_n_read(function, chunk, (uint16_t)requested);
        if (!received || received > requested || received % USB_AUDIO_FRAME_BYTES) {
            tud_audio_n_clear_ep_out_ff(function);
            count(&s_output_errors, 1);
            break;
        }
        unsigned frames = received / USB_AUDIO_FRAME_BYTES;
        portENTER_CRITICAL_ISR(&s_capture_lock);
        if (s_capture.started && s_capture.speaker_open) {
            if (s_capture.count + frames > USB_RING_FRAMES) {
                unsigned dropped = s_capture.count + frames - USB_RING_FRAMES;
                s_capture.head = (s_capture.head + dropped) % USB_RING_FRAMES;
                s_capture.count -= dropped;
                ++s_capture.generation;
                count(&s_capture_dropped_frames, dropped);
            }
            unsigned tail = (s_capture.head + s_capture.count) % USB_RING_FRAMES;
            unsigned first = USB_RING_FRAMES - tail;
            if (first > frames)
                first = frames;
            memcpy(&s_ring[tail * 2], chunk, first * USB_AUDIO_FRAME_BYTES);
            if (first < frames)
                memcpy(s_ring, chunk + first * 2, (frames - first) * USB_AUDIO_FRAME_BYTES);
            s_capture.count += frames;
        } else {
            count(&s_discarded_bytes, received);
        }
        portEXIT_CRITICAL_ISR(&s_capture_lock);
        remaining -= received;
    }
    return true;
}

void tud_audio_feedback_params_cb(uint8_t function, uint8_t alternate,
                                  audio_feedback_params_t *feedback) {
    (void)alternate;
    feedback->method = AUDIO_FEEDBACK_METHOD_DISABLED;
    feedback->sample_freq = USB_AUDIO_RATE;
    uint32_t intervals = tud_speed_get() == TUSB_SPEED_HIGH ? 8000 : 1000;
    tud_audio_n_fb_set(function, atomic_load_explicit(&s_feedback_rate_q16, memory_order_relaxed) /
                                     intervals);
}

/* Reuse the passed UAC diagnostic's endpoint observation and first-HS-feedback
 * workaround. The wrappers forward every CDC and other endpoint unchanged. */
extern bool __real_dcd_edpt_xfer(uint8_t rhport, uint8_t endpoint, uint8_t *buffer, uint16_t bytes);
extern void __real_dcd_event_handler(const dcd_event_t *event, bool in_isr);
extern bool __real_usbd_edpt_xfer_fifo(uint8_t rhport, uint8_t endpoint, tu_fifo_t *fifo,
                                       uint16_t bytes);

/* TinyUSB chooses a size from ep_in_ff before invoking tx_done_isr. With the
 * pinned PIO driver that FIFO is consumed later, in another interrupt. Bypass
 * it only for microphone: each scheduled transfer gets exactly one nominal
 * packet and a stable internal buffer. No stale shared-buffer tails and no
 * FreeRTOS tick clock enter the USB IN stream. */
bool __wrap_usbd_edpt_xfer_fifo(uint8_t rhport, uint8_t endpoint, tu_fifo_t *fifo, uint16_t bytes) {
    if (rhport != 0 || endpoint != MYBOT_USB_EP_AUDIO_IN)
        return __real_usbd_edpt_xfer_fifo(rhport, endpoint, fifo, bytes);
    unsigned frames = tud_speed_get() == TUSB_SPEED_HIGH ? 6 : 48;
    unsigned available = 0, peak = 0;
    memset(s_microphone_packet, 0, frames * USB_AUDIO_FRAME_BYTES);
    portENTER_CRITICAL_ISR(&s_playback_lock);
    bool running = s_playback.started && s_playback.microphone_open;
    if (running) {
        if (s_playback.priming && s_playback.count) {
            /* Wait 20 ms from the first translated PCM, even for a short
             * utterance. This gives 60 ms RTC bursts 20 ms of jitter reserve. */
            if (s_playback.prime_frames <= frames) {
                s_playback.priming = false;
                s_playback.prime_frames = 0;
            } else {
                s_playback.prime_frames -= frames;
            }
        }
        if (!s_playback.priming) {
            available = s_playback.count < frames ? s_playback.count : frames;
            for (unsigned i = 0; i < available; ++i) {
                int16_t sample = s_playback_ring[s_playback.head];
                s_playback.head = (s_playback.head + 1) % USB_RING_FRAMES;
                s_microphone_packet[i * 2] = s_microphone_packet[i * 2 + 1] = sample;
                unsigned value = sample < 0 ? -(int)sample : sample;
                if (value > peak)
                    peak = value;
            }
            s_playback.count -= available;
            if (available < frames)
                clear_playback_locked();
        }
        count(&s_playback_frames, available);
        count(&s_playback_zero_frames, frames - available);
    }
    atomic_store_explicit(&s_playback_peak, peak, memory_order_relaxed);
    /* Serialize stop against packet publication, but never modify this buffer
     * again until the next transfer is scheduled after completion. A packet
     * already in hardware can finish after stop; it contains 125 us HS / 1 ms
     * FS of audio. ISO retries may extend its wall-clock completion time. */
    bool submitted = usbd_edpt_xfer(rhport, endpoint, (uint8_t *)s_microphone_packet,
                                    frames * USB_AUDIO_FRAME_BYTES);
    portEXIT_CRITICAL_ISR(&s_playback_lock);
    count(&s_input_callbacks, 1);
    count(&s_silence_bytes, (frames - available) * USB_AUDIO_FRAME_BYTES);
    if (!submitted)
        count(&s_input_errors, 1);
    return submitted;
}

bool tud_audio_tx_done_isr(uint8_t rhport, uint16_t bytes, uint8_t function, uint8_t endpoint,
                           uint8_t alternate) {
    (void)bytes;
    (void)alternate;
    return rhport == 0 && function == 0 && endpoint == MYBOT_USB_EP_AUDIO_IN;
}

bool __wrap_dcd_edpt_xfer(uint8_t rhport, uint8_t endpoint, uint8_t *buffer, uint16_t bytes) {
    if (rhport != 0 || endpoint != MYBOT_USB_EP_AUDIO_FEEDBACK)
        return __real_dcd_edpt_xfer(rhport, endpoint, buffer, bytes);
    /* This also covers TinyUSB's first packet before its parameter callback.
     * FS uses 10.14 / 1 ms; HS uses 16.16 / 125 us. */
    uint32_t rate = atomic_load_explicit(&s_feedback_rate_q16, memory_order_relaxed);
    if (buffer && bytes == 4 && tud_speed_get() == TUSB_SPEED_HIGH) {
        uint32_t value = rate / 8000;
        tu_unaligned_write32(buffer, tu_htole32(value));
        atomic_store_explicit(&s_fb_value, value, memory_order_relaxed);
    } else if (buffer && bytes == 3 && tud_speed_get() == TUSB_SPEED_FULL) {
        uint32_t value = rate / 4000;
        buffer[0] = (uint8_t)value;
        buffer[1] = (uint8_t)(value >> 8);
        buffer[2] = (uint8_t)(value >> 16);
        atomic_store_explicit(&s_fb_value, value, memory_order_relaxed);
    }
    count(&s_fb_attempts, 1);
    bool submitted = __real_dcd_edpt_xfer(rhport, endpoint, buffer, bytes);
    count(submitted ? &s_fb_submitted : &s_fb_submit_errors, 1);
    return submitted;
}

void __wrap_dcd_event_handler(const dcd_event_t *event, bool in_isr) {
    if (event && event->rhport == 0 && event->event_id == DCD_EVENT_XFER_COMPLETE) {
        bool success = event->xfer_complete.result == XFER_RESULT_SUCCESS;
        unsigned bytes = event->xfer_complete.len;
        switch (event->xfer_complete.ep_addr) {
        case MYBOT_USB_EP_AUDIO_FEEDBACK:
            count(&s_fb_completed, 1);
            count(success ? &s_fb_bytes : &s_fb_errors, success ? bytes : 1);
            break;
        case MYBOT_USB_EP_AUDIO_IN:
            count(&s_mic_completed, 1);
            count(success ? &s_mic_bytes : &s_mic_errors, success ? bytes : 1);
            break;
        case MYBOT_USB_EP_AUDIO_OUT:
            if (success) {
                count(&s_rx_packets, 1);
                count(&s_rx_bytes, bytes);
                if (!bytes)
                    count(&s_rx_zero_packets, 1);
            } else {
                count(&s_out_errors, 1);
            }
            break;
        default:
            break;
        }
    }
    __real_dcd_event_handler(event, in_isr);
}

mosaico_usb_audio_stats_t mosaico_usb_audio_stats(void) {
    mosaico_usb_audio_stats_t stats = {
        .prepared = atomic_load_explicit(&s_prepared, memory_order_acquire),
        .mounted = tud_mounted(),
        .suspended = tud_suspended(),
#define SNAPSHOT(name) .name = atomic_load_explicit(&s_##name, memory_order_relaxed)
        SNAPSHOT(mounts),
        SNAPSHOT(unmounts),
        SNAPSHOT(suspends),
        SNAPSHOT(resumes),
        SNAPSHOT(rx_packets),
        SNAPSHOT(rx_bytes),
        SNAPSHOT(rx_zero_packets),
        SNAPSHOT(out_errors),
        SNAPSHOT(output_callbacks),
        SNAPSHOT(discarded_bytes),
        SNAPSHOT(output_errors),
        SNAPSHOT(input_callbacks),
        SNAPSHOT(silence_bytes),
        SNAPSHOT(input_errors),
        SNAPSHOT(mic_completed),
        SNAPSHOT(mic_bytes),
        SNAPSHOT(mic_errors),
        SNAPSHOT(fb_attempts),
        SNAPSHOT(fb_submitted),
        SNAPSHOT(fb_submit_errors),
        SNAPSHOT(fb_completed),
        SNAPSHOT(fb_bytes),
        SNAPSHOT(fb_errors),
        SNAPSHOT(fb_value),
        SNAPSHOT(muted),
        SNAPSHOT(volume),
        SNAPSHOT(capture_frames),
        SNAPSHOT(capture_zero_frames),
        SNAPSHOT(capture_dropped_frames),
        SNAPSHOT(capture_peak),
        SNAPSHOT(playback_frames),
        SNAPSHOT(playback_zero_frames),
        SNAPSHOT(playback_dropped_frames),
        SNAPSHOT(playback_peak),
#undef SNAPSHOT
    };
    portENTER_CRITICAL_ISR(&s_capture_lock);
    stats.capture_started = s_capture.started;
    stats.speaker_open = s_capture.speaker_open;
    stats.microphone_open = s_capture.microphone_open;
    stats.queued_frames = s_capture.count;
    portEXIT_CRITICAL_ISR(&s_capture_lock);
    portENTER_CRITICAL_ISR(&s_playback_lock);
    stats.playback_started = s_playback.started;
    stats.playback_queued_frames = s_playback.count;
    portEXIT_CRITICAL_ISR(&s_playback_lock);
    stats.ready = stats.prepared && stats.mounted && !stats.suspended;
    return stats;
}

void mosaico_usb_audio_log_stats(void) {
    mosaico_usb_audio_stats_t stats = mosaico_usb_audio_stats();
    const char *speed = tud_speed_get() == TUSB_SPEED_HIGH   ? "high"
                        : tud_speed_get() == TUSB_SPEED_FULL ? "full"
                                                             : "none";
    ESP_LOGI(
        TAG,
        "event=usb_audio_status prepared=%u ready=%u mounted=%u suspended=%u speed=%s "
        "mounts=%u unmounts=%u suspends=%u resumes=%u "
        "rx_packets=%u rx_bytes=%u rx_zero=%u out_errors=%u "
        "mic_completed=%u mic_bytes=%u mic_errors=%u "
        "fb_submitted=%u fb_submit_errors=%u fb_completed=%u fb_bytes=%u fb_errors=%u fb_value=%u",
        stats.prepared, stats.ready, stats.mounted, stats.suspended, speed, stats.mounts,
        stats.unmounts, stats.suspends, stats.resumes, stats.rx_packets, stats.rx_bytes,
        stats.rx_zero_packets, stats.out_errors, stats.mic_completed, stats.mic_bytes,
        stats.mic_errors, stats.fb_submitted, stats.fb_submit_errors, stats.fb_completed,
        stats.fb_bytes, stats.fb_errors, stats.fb_value);
    ESP_LOGI(TAG,
             "event=usb_audio_callbacks output_calls=%u discarded_bytes=%u output_errors=%u "
             "input_calls=%u silence_bytes=%u input_errors=%u usb_mute=%u usb_volume=%u "
             "routing=%s speaker_open=%u mic_open=%u queued_frames48=%u "
             "capture_frames16=%u zero_frames16=%u dropped_frames48=%u peak16=%u "
             "return_started=%u return_queued48=%u return_frames48=%u return_zero48=%u "
             "return_dropped48=%u return_peak=%u",
             stats.output_callbacks, stats.discarded_bytes, stats.output_errors,
             stats.input_callbacks, stats.silence_bytes, stats.input_errors, stats.muted,
             stats.volume,
             stats.playback_started  ? "usb_listen_speak"
             : stats.capture_started ? "usb_listen_and_silence"
                                     : "discard_and_silence",
             stats.speaker_open, stats.microphone_open, stats.queued_frames, stats.capture_frames,
             stats.capture_zero_frames, stats.capture_dropped_frames, stats.capture_peak,
             stats.playback_started, stats.playback_queued_frames, stats.playback_frames,
             stats.playback_zero_frames, stats.playback_dropped_frames, stats.playback_peak);
}
