// SPDX-FileCopyrightText: 2026 MyBot Project Contributors
// SPDX-License-Identifier: Apache-2.0

#include "usb_audio.h"
#include "usb_transfer.h"

#include <math.h>
#include <inttypes.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "audio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_private/usb_phy.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "tusb.h"
#include "usb_device_uac.h"

static const char *TAG = "mosaico_uac";
static usb_phy_handle_t s_phy;
static bool s_start_attempted;
static atomic_bool s_muted;
static atomic_uint s_gain_q15 = 32768;
static atomic_uint s_playback_bytes;
static atomic_uint s_capture_bytes;
static atomic_uint s_playback_errors;
static atomic_uint s_capture_errors;
static atomic_uint s_playback_dropped;
static atomic_uint s_rx_packets;
static atomic_uint s_rx_bytes;
static atomic_uint s_rx_zero_packets;
static atomic_uint s_playback_callbacks;
static atomic_uint s_callback_bytes;
static atomic_uint s_raw_peak;
static atomic_uint s_played_peak;
static atomic_uint s_queued_blocks;
static portMUX_TYPE s_playback_copy_lock = portMUX_INITIALIZER_UNLOCKED;
static QueueHandle_t s_playback_queue;

static uint8_t feedback_interval(bool other_speed) {
    bool high_speed = tud_speed_get() == TUSB_SPEED_HIGH;
    if (other_speed)
        high_speed = !high_speed;
    /* Match TinyUSB's UAC2 speaker example: 1 ms at HS (4), 1 ms at FS (1). */
    return high_speed ? 4 : 1;
}

/* Four 5 ms blocks keep the component's callback independent of I2S DMA waits. */
typedef struct {
    int16_t samples[MOSAICO_AUDIO_SAMPLE_RATE / 1000 * MOSAICO_AUDIO_CHANNELS * 5];
} playback_block_t;

_Static_assert(CONFIG_UAC_SPK_TASK_CORE == 0, "Playback worker must share the USB IRQ core");
_Static_assert(TUD_AUDIO_PREFER_RING_BUFFER && !CFG_TUD_DWC2_DMA_ENABLE &&
                   !CFG_TUD_MEM_DCACHE_ENABLE,
               "Use DWC2 PIO FIFO audio, including zero-length OUT handling");
_Static_assert(CONFIG_UAC_SAMPLE_RATE == MOSAICO_AUDIO_SAMPLE_RATE &&
                   CONFIG_UAC_SPEAKER_CHANNEL_NUM == MOSAICO_AUDIO_CHANNELS &&
                   CONFIG_UAC_MIC_CHANNEL_NUM == MOSAICO_AUDIO_CHANNELS &&
                   CONFIG_UAC_BIT_RESOLUTION == MOSAICO_AUDIO_BITS,
               "USB and codec PCM formats must match");

/* Count the real USB completions before the component buffers or dispatches them. */
extern bool mosaico_uac_upstream_rx_done_isr(uint8_t rhport, uint16_t bytes, uint8_t function,
                                             uint8_t endpoint, uint8_t alternate);

bool tud_audio_rx_done_isr(uint8_t rhport, uint16_t bytes, uint8_t function, uint8_t endpoint,
                           uint8_t alternate) {
    atomic_fetch_add(&s_rx_packets, 1);
    atomic_fetch_add(&s_rx_bytes, bytes);
    if (bytes == 0)
        atomic_fetch_add(&s_rx_zero_packets, 1);
    return mosaico_uac_upstream_rx_done_isr(rhport, bytes, function, endpoint, alternate);
}

static unsigned pcm_peak(const int16_t *samples, size_t count) {
    unsigned peak = 0;
    for (size_t i = 0; i < count; ++i) {
        int32_t sample = samples[i];
        unsigned magnitude = (unsigned)(sample < 0 ? -sample : sample);
        if (magnitude > peak)
            peak = magnitude;
    }
    return peak;
}

static esp_err_t playback(uint8_t *data, size_t bytes, void *context) {
    (void)context;
    static uint8_t snapshot[CFG_TUD_AUDIO_FUNC_1_EP_OUT_SW_BUF_SZ];
    static playback_block_t pending;
    static size_t pending_bytes;
    static int64_t last_packet_us;
    atomic_fetch_add(&s_playback_callbacks, 1);
    atomic_fetch_add(&s_callback_bytes, bytes);
    if (bytes == 0 || bytes > sizeof(snapshot) || bytes % MOSAICO_AUDIO_FRAME_BYTES != 0) {
        atomic_fetch_add(&s_playback_errors, 1);
        return ESP_ERR_INVALID_SIZE;
    }
    /* The component reuses its receive buffer in the USB interrupt. Both the
     * interrupt and this worker run on core 0; mask it only for this bounded
     * copy. The separate playback worker owns all blocking codec I/O. */
    portENTER_CRITICAL(&s_playback_copy_lock);
    memcpy(snapshot, data, bytes);
    portEXIT_CRITICAL(&s_playback_copy_lock);
    /* The local snapshot is byte-aligned; use memcpy for each S16 sample. */
    unsigned peak = 0;
    for (size_t i = 0; i < bytes; i += sizeof(int16_t)) {
        int16_t sample;
        memcpy(&sample, snapshot + i, sizeof(sample));
        unsigned magnitude = pcm_peak(&sample, 1);
        if (magnitude > peak)
            peak = magnitude;
    }
    if (peak > atomic_load(&s_raw_peak))
        atomic_store(&s_raw_peak, peak);
    int64_t now = esp_timer_get_time();
    if (now - last_packet_us > 100000) {
        pending_bytes = 0;
    }
    last_packet_us = now;
    size_t offset = 0;
    while (offset < bytes) {
        size_t chunk = sizeof(pending) - pending_bytes;
        if (chunk > bytes - offset) {
            chunk = bytes - offset;
        }
        memcpy((uint8_t *)&pending + pending_bytes, snapshot + offset, chunk);
        pending_bytes += chunk;
        offset += chunk;
        if (pending_bytes == sizeof(pending)) {
            if (xQueueSend(s_playback_queue, &pending, 0) != pdPASS) {
                atomic_fetch_add(&s_playback_dropped, 1);
            } else {
                atomic_fetch_add(&s_queued_blocks, 1);
            }
            pending_bytes = 0;
        }
    }
    return ESP_OK;
}

static void playback_task(void *context) {
    (void)context;
    playback_block_t block;
    for (;;) {
        if (xQueueReceive(s_playback_queue, &block, portMAX_DELAY) != pdPASS) {
            continue;
        }
        if (!tud_mounted() || tud_suspended()) {
            continue;
        }
        unsigned gain = atomic_load(&s_muted) ? 0 : atomic_load(&s_gain_q15);
        for (size_t i = 0; i < sizeof(block.samples) / sizeof(block.samples[0]); ++i) {
            block.samples[i] = (int16_t)((int32_t)block.samples[i] * (int32_t)gain / 32768);
        }
        esp_err_t result = mosaico_audio_write(block.samples, sizeof(block.samples));
        if (result == ESP_OK) {
            atomic_fetch_add(&s_playback_bytes, sizeof(block.samples));
            unsigned peak =
                pcm_peak(block.samples, sizeof(block.samples) / sizeof(block.samples[0]));
            if (peak > atomic_load(&s_played_peak))
                atomic_store(&s_played_peak, peak);
        } else {
            atomic_fetch_add(&s_playback_errors, 1);
        }
    }
}

static esp_err_t capture(uint8_t *data, size_t bytes, size_t *bytes_read, void *context) {
    (void)context;
    esp_err_t result = mosaico_audio_read(data, bytes);
    if (result == ESP_OK) {
        atomic_fetch_add(&s_capture_bytes, bytes);
    } else {
        /* Send silence after a failed/partial read. Returning success also lets the
         * component's capture worker keep its 10 ms cadence instead of busy retrying. */
        memset(data, 0, bytes);
        atomic_fetch_add(&s_capture_errors, 1);
    }
    *bytes_read = bytes;
    return ESP_OK;
}

static void set_mute(uint32_t mute, void *context) {
    (void)context;
    atomic_store(&s_muted, mute != 0);
}

static void set_volume(uint32_t volume, void *context) {
    (void)context;
    if (volume > 100) {
        volume = 100;
    }
    /* usb_device_uac maps its -50..0 dB control to 0..100. Apply that attenuation
     * in the playback worker; keep codec I2C operations out of the USB task. */
    float db = (float)volume / 2.0f - 50.0f;
    atomic_store(&s_gain_q15, (unsigned)(32768.0f * powf(10.0f, db / 20.0f)));
}

void tud_audio_feedback_params_cb(uint8_t function, uint8_t alternate,
                                  audio_feedback_params_t *feedback) {
    (void)alternate;
    /* This short hardware test uses nominal 48 kHz feedback. Production audio
     * will need feedback tied to actual codec consumption/clock drift. */
    feedback->method = AUDIO_FEEDBACK_METHOD_DISABLED;
    feedback->sample_freq = MOSAICO_AUDIO_SAMPLE_RATE;
    uint32_t frames_per_second = tud_speed_get() == TUSB_SPEED_HIGH ? 8000 : 1000;
    tud_audio_n_fb_set(function, ((uint32_t)MOSAICO_AUDIO_SAMPLE_RATE << 16) / frames_per_second);
}

static void usb_task(void *context) {
    (void)context;
    for (;;) {
        tud_task();
    }
}

esp_err_t mosaico_usb_audio_start(void) {
    ESP_RETURN_ON_FALSE(!s_start_attempted, ESP_ERR_INVALID_STATE, TAG, "already attempted");
    ESP_RETURN_ON_FALSE(xPortGetCoreID() == 0, ESP_ERR_INVALID_STATE, TAG,
                        "USB IRQ must be installed on core 0");
    s_start_attempted = true;
    s_playback_queue = xQueueCreate(4, sizeof(playback_block_t));
    ESP_RETURN_ON_FALSE(s_playback_queue, ESP_ERR_NO_MEM, TAG, "create playback queue");

    /* The UAC component's default PHY helper selects INT. S31's native USB2
     * controller uses UTMI on port 0, as in the official Mosaico USB example. */
    const usb_phy_config_t phy_config = {
        .controller = USB_PHY_CTRL_OTG,
        .target = USB_PHY_TARGET_UTMI,
        .otg_mode = USB_OTG_MODE_DEVICE,
        .otg_speed = USB_PHY_SPEED_HIGH,
    };
    ESP_RETURN_ON_ERROR(usb_new_phy(&phy_config, &s_phy), TAG, "initialize UTMI PHY");
    const tusb_rhport_init_t port_config = {
        .role = TUSB_ROLE_DEVICE,
        .speed = TUSB_SPEED_HIGH,
    };
    ESP_RETURN_ON_FALSE(tusb_init(0, &port_config), ESP_FAIL, TAG, "initialize TinyUSB");
    uac_device_config_t uac_config = {
        .skip_tinyusb_init = true,
        .output_cb = playback,
        .input_cb = capture,
        .set_mute_cb = set_mute,
        .set_volume_cb = set_volume,
    };
    ESP_RETURN_ON_ERROR(uac_device_init(&uac_config), TAG, "initialize UAC workers");
    ESP_RETURN_ON_FALSE(xTaskCreate(playback_task, "mosaico_spk", 4096, NULL, 5, NULL) == pdPASS,
                        ESP_ERR_NO_MEM, TAG, "create playback task");
    ESP_RETURN_ON_FALSE(xTaskCreate(usb_task, "mosaico_usb", 4096, NULL, 5, NULL) == pdPASS,
                        ESP_ERR_NO_MEM, TAG, "create USB task");
    ESP_LOGI(TAG, "event=uac_ready rate=48000 bits=16 speaker_channels=2 mic_channels=2");
    return ESP_OK;
}

void mosaico_usb_audio_log_stats(void) {
    tusb_speed_t speed = tud_speed_get();
    const char *speed_name = speed == TUSB_SPEED_HIGH   ? "high"
                             : speed == TUSB_SPEED_FULL ? "full"
                                                        : "none";
    ESP_LOGI(TAG,
             "event=uac_status mounted=%d speed=%s playback_bytes=%u capture_bytes=%u "
             "playback_errors=%u capture_errors=%u playback_dropped_blocks=%u "
             "usb_muted=%u usb_gain_q15=%u",
             tud_mounted(), speed_name, atomic_load(&s_playback_bytes),
             atomic_load(&s_capture_bytes), atomic_load(&s_playback_errors),
             atomic_load(&s_capture_errors), atomic_load(&s_playback_dropped),
             (unsigned)atomic_load(&s_muted), atomic_load(&s_gain_q15));
}

/* Read-only status over endpoint zero. No extra interface or UART is required.
 * Keep the response alive until TinyUSB completes all control-transfer stages. */
bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage,
                                tusb_control_request_t const *request) {
    if (request->bmRequestType != 0xc0 || request->bRequest != 0x5a ||
        tu_le16toh(request->wValue) != 1 || tu_le16toh(request->wIndex) != 0 ||
        tu_le16toh(request->wLength) == 0)
        return false;
    if (stage != CONTROL_STAGE_SETUP)
        return true;
    static char response[1024];
    mosaico_usb_transfer_stats_t transfer = mosaico_usb_transfer_stats();
    int bytes = snprintf(
        response, sizeof(response),
        "{\"version\":1,\"codec_volume\":100,\"fb_interval\":%u,\"mounted\":%u,\"suspended\":%u,"
        "\"rx_packets\":%u,\"rx_bytes\":%u,\"zero_packets\":%u,"
        "\"callbacks\":%u,\"callback_bytes\":%u,\"raw_peak\":%u,"
        "\"queued_blocks\":%u,\"played_bytes\":%u,\"played_peak\":%u,"
        "\"play_errors\":%u,\"dropped_blocks\":%u,\"capture_bytes\":%u,"
        "\"capture_errors\":%u,\"muted\":%u,\"gain_q15\":%u,"
        "\"fb_attempts\":%" PRIu32 ",\"fb_submitted\":%" PRIu32 ",\"fb_submit_errors\":%" PRIu32
        ",\"fb_completed\":%" PRIu32 ",\"fb_errors\":%" PRIu32 ",\"fb_bytes\":%" PRIu32
        ",\"fb_value\":%" PRIu32 ",\"mic_completed\":%" PRIu32 ",\"mic_errors\":%" PRIu32
        ",\"mic_bytes\":%" PRIu32 ",\"out_errors\":%" PRIu32 "}",
        (unsigned)feedback_interval(false), (unsigned)tud_mounted(), (unsigned)tud_suspended(),
        atomic_load(&s_rx_packets), atomic_load(&s_rx_bytes), atomic_load(&s_rx_zero_packets),
        atomic_load(&s_playback_callbacks), atomic_load(&s_callback_bytes),
        atomic_load(&s_raw_peak), atomic_load(&s_queued_blocks), atomic_load(&s_playback_bytes),
        atomic_load(&s_played_peak), atomic_load(&s_playback_errors),
        atomic_load(&s_playback_dropped), atomic_load(&s_capture_bytes),
        atomic_load(&s_capture_errors), (unsigned)atomic_load(&s_muted), atomic_load(&s_gain_q15),
        transfer.fb_attempts, transfer.fb_submitted, transfer.fb_submit_errors,
        transfer.fb_completed, transfer.fb_errors, transfer.fb_bytes, transfer.fb_value,
        transfer.mic_completed, transfer.mic_errors, transfer.mic_bytes, transfer.out_errors);
    if (bytes <= 0 || (size_t)bytes >= sizeof(response))
        return false;
    return tud_control_xfer(rhport, request, response, (uint16_t)bytes);
}

/* The component supplies the descriptor topology. Override only the feedback
 * interval in a lifetime-static copy, without editing the managed component. */
extern uint8_t const *__real_tud_descriptor_configuration_cb(uint8_t index);

static bool set_feedback_interval(uint8_t *configuration, size_t size, uint8_t interval) {
    size_t offset = 0;
    unsigned found = 0;
    while (offset + 2 <= size) {
        uint8_t length = configuration[offset];
        if (length < 2 || length > size - offset)
            return false;
        if (configuration[offset + 1] == TUSB_DESC_ENDPOINT && length >= 7 &&
            (configuration[offset + 2] & 0x80) && (configuration[offset + 3] & 0x3f) == 0x11) {
            configuration[offset + 6] = interval;
            ++found;
        }
        offset += length;
    }
    return offset == size && found == 1;
}

uint8_t const *__wrap_tud_descriptor_configuration_cb(uint8_t index) {
    static uint8_t configuration[TUD_CONFIG_DESC_LEN + CFG_TUD_AUDIO_FUNC_1_DESC_LEN];
    if (index != 0)
        return NULL;
    const uint8_t *original = __real_tud_descriptor_configuration_cb(index);
    if (!original)
        return NULL;
    memcpy(configuration, original, sizeof(configuration));
    if (!set_feedback_interval(configuration, sizeof(configuration), feedback_interval(false)))
        return NULL;
    return configuration;
}

/* The component omits USB2 qualifier callbacks. Endpoint packet sizes are
 * valid at both speeds; feedback's bInterval is adjusted for each speed. */
uint8_t const *tud_descriptor_device_qualifier_cb(void) {
    static const tusb_desc_device_qualifier_t qualifier = {
        .bLength = sizeof(tusb_desc_device_qualifier_t),
        .bDescriptorType = TUSB_DESC_DEVICE_QUALIFIER,
        .bcdUSB = 0x0200,
        .bDeviceClass = TUSB_CLASS_MISC,
        .bDeviceSubClass = MISC_SUBCLASS_COMMON,
        .bDeviceProtocol = MISC_PROTOCOL_IAD,
        .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
        .bNumConfigurations = 1,
    };
    return (const uint8_t *)&qualifier;
}

uint8_t const *tud_descriptor_other_speed_configuration_cb(uint8_t index) {
    static uint8_t other_speed[TUD_CONFIG_DESC_LEN + CFG_TUD_AUDIO_FUNC_1_DESC_LEN];
    if (index != 0)
        return NULL;
    const uint8_t *configuration = __real_tud_descriptor_configuration_cb(index);
    if (!configuration)
        return NULL;
    memcpy(other_speed, configuration, sizeof(other_speed));
    other_speed[1] = TUSB_DESC_OTHER_SPEED_CONFIG;
    if (!set_feedback_interval(other_speed, sizeof(other_speed), feedback_interval(true)))
        return NULL;
    return other_speed;
}
