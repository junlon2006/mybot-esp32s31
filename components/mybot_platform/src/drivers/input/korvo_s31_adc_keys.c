/* SPDX-License-Identifier: Apache-2.0 */
#include <mybot/mybot.h>
#include <mybot/platform/mybot_key.h>
#include "mybot_platform/board.h"
#include "input/korvo_s31_keys.h"
#include "bsp_adc_calibration.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

enum { KEY_NONE = -1, KEY_UP, KEY_DOWN, KEY_PLAY, KEY_SET };
static adc_oneshot_unit_handle_t s_adc;
static TaskHandle_t s_task;
static StaticSemaphore_t s_callback_storage;
static SemaphoreHandle_t s_callback_lock;
static mybot_key_event_handler_t s_emit;
static void *s_user;
/* Only 32-bit atomics are needed on RV32. Odd epochs mean SDK attached; each
 * attach/detach advances the epoch so a touch cannot cross SDK lifetimes. */
static uint32_t s_epoch;
static uint32_t s_touch_pending;

void mybot_korvo_s31_keys_touch_sample(bool valid, bool pressed) {
    /* Gesture state has one owner: the LVGL task. Require a clean release after
     * display initialization or an I2C error before accepting a new press. */
    static bool released;
    static uint32_t gesture;
    if (!valid) {
        released = false;
        gesture = 0;
        return;
    }
    if (pressed) {
        if (released) {
            released = false;
            uint32_t epoch = __atomic_load_n(&s_epoch, __ATOMIC_ACQUIRE);
            mybot_state_t state = mybot_get_state();
            if ((epoch & 1U) &&
                (state == MYBOT_STATE_READY || state == MYBOT_STATE_IN_CONVERSATION)) {
                gesture = (epoch << 1) | (state == MYBOT_STATE_IN_CONVERSATION ? 1U : 0U);
            }
        }
        return;
    }
    released = true;
    uint32_t pending = gesture;
    gesture = 0;
    if (!pending || (pending >> 1) != __atomic_load_n(&s_epoch, __ATOMIC_ACQUIRE)) {
        return;
    }
    mybot_state_t expected = (pending & 1U) ? MYBOT_STATE_IN_CONVERSATION : MYBOT_STATE_READY;
    if (mybot_get_state() != expected) {
        return;
    }
    /* One bounded mailbox, no allocation, mutex or SDK callback while LVGL is
     * locked. Repeated taps never accumulate commands behind a slow callback. */
    uint32_t empty = 0;
    (void)__atomic_compare_exchange_n(&s_touch_pending, &empty, pending, false, __ATOMIC_RELEASE,
                                      __ATOMIC_RELAXED);
}

static void dispatch_touch(void) {
    if (!__atomic_load_n(&s_touch_pending, __ATOMIC_ACQUIRE)) {
        return;
    }
    xSemaphoreTake(s_callback_lock, portMAX_DELAY);
    uint32_t pending = __atomic_exchange_n(&s_touch_pending, 0, __ATOMIC_ACQ_REL);
    if (pending && s_emit && (pending >> 1) == __atomic_load_n(&s_epoch, __ATOMIC_ACQUIRE)) {
        bool stop = (pending & 1U) != 0;
        mybot_state_t expected = stop ? MYBOT_STATE_IN_CONVERSATION : MYBOT_STATE_READY;
        if (mybot_get_state() == expected) {
            ESP_LOGI("korvo_s31_keys", "event=touch action=conversation_%s",
                     stop ? "stop" : "start");
            s_emit(stop ? MYBOT_KEY_EVENT_CONVERSATION_STOP : MYBOT_KEY_EVENT_CONVERSATION_START,
                   s_user);
        }
    }
    xSemaphoreGive(s_callback_lock);
}

static int classify_key(int mv) {
    /* Midpoints of the pinned BSP's 380/820/1340/1870 mV ladder and 2000 mV idle. */
    if (mv < 160 || mv >= 1935)
        return KEY_NONE;
    if (mv < 600)
        return KEY_UP;
    if (mv < 1080)
        return KEY_DOWN;
    if (mv < 1605)
        return KEY_PLAY;
    return KEY_SET;
}

static void short_press(int key) {
    xSemaphoreTake(s_callback_lock, portMAX_DELAY);
    if (s_emit) {
        switch (key) {
        case KEY_UP:
            s_emit(MYBOT_KEY_EVENT_VOLUME_UP, s_user);
            break;
        case KEY_DOWN:
            s_emit(MYBOT_KEY_EVENT_VOLUME_DOWN, s_user);
            break;
        case KEY_SET:
            s_emit(MYBOT_KEY_EVENT_PAIR, s_user);
            break;
        case KEY_PLAY: {
            mybot_state_t state = mybot_get_state();
            if (state == MYBOT_STATE_READY)
                s_emit(MYBOT_KEY_EVENT_CONVERSATION_START, s_user);
            else if (state == MYBOT_STATE_IN_CONVERSATION)
                s_emit(MYBOT_KEY_EVENT_CONVERSATION_STOP, s_user);
            break;
        }
        default:
            break;
        }
    }
    xSemaphoreGive(s_callback_lock);
}

static void key_task(void *arg) {
    (void)arg;
    int candidate = KEY_NONE, stable = KEY_NONE;
    unsigned samples = 0;
    int64_t pressed_at = 0;
    bool long_sent = false;
    for (;;) {
        int raw = 0, mv = 0;
        bool valid = adc_oneshot_read(s_adc, ADC_CHANNEL_0, &raw) == ESP_OK &&
                     bsp_s31_adc_calibration_raw_to_mv(ADC_UNIT_1, raw, &mv) == ESP_OK;
        dispatch_touch();
        int key = valid ? classify_key(mv) : KEY_NONE;
        int64_t now = esp_timer_get_time() / 1000;
        if (!valid) {
            /* A failed sample cancels a press rather than synthesizing a release. */
            stable = candidate = KEY_NONE;
            samples = 0;
        } else if (key != candidate) {
            candidate = key;
            samples = 1;
        } else if (samples < 3) {
            ++samples;
        }
        if (valid && samples == 3 && stable != candidate) {
            if (stable != KEY_NONE && candidate == KEY_NONE && !long_sent && now - pressed_at >= 60)
                short_press(stable);
            stable = candidate;
            pressed_at = now;
            long_sent = false;
            if (stable != KEY_NONE)
                ESP_LOGI("korvo_s31_keys", "event=key key=%d voltage_mv=%d", stable, mv);
        }
        if (stable == KEY_SET && !long_sent && now - pressed_at >= 3000) {
            long_sent = true;
            mybot_board_request_wifi_provisioning();
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

int mybot_korvo_s31_keys_start(void) {
    if (s_task)
        return 0;
    /* An earlier failed initialization may still own an ADC handle when its
     * cleanup failed. Retain and retry it before asking IDF for another unit. */
    if (s_adc) {
        if (adc_oneshot_del_unit(s_adc) != ESP_OK)
            return -1;
        s_adc = NULL;
    }
    if (!s_callback_lock)
        s_callback_lock = xSemaphoreCreateMutexStatic(&s_callback_storage);
    const adc_oneshot_unit_init_cfg_t unit = {.unit_id = ADC_UNIT_1};
    const adc_oneshot_chan_cfg_t channel = {.atten = ADC_ATTEN_DB_0,
                                            .bitwidth = ADC_BITWIDTH_DEFAULT};
    if (!s_callback_lock)
        return -1;
    if (adc_oneshot_new_unit(&unit, &s_adc) != ESP_OK ||
        adc_oneshot_config_channel(s_adc, ADC_CHANNEL_0, &channel) != ESP_OK ||
        bsp_s31_adc_calibration_init(s_adc, ADC_UNIT_1, ADC_CHANNEL_0) != ESP_OK ||
        xTaskCreateWithCaps(key_task, "korvo_keys", 3072, NULL, 4, &s_task,
                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        if (s_adc && adc_oneshot_del_unit(s_adc) == ESP_OK)
            s_adc = NULL;
        return -1;
    }
    return 0;
}

static int keys_init(void **out, mybot_key_event_handler_t emit, void *user) {
    if (!out)
        return -1;
    *out = NULL;
    if (!emit || !s_task)
        return -1;
    xSemaphoreTake(s_callback_lock, portMAX_DELAY);
    if (s_emit) {
        xSemaphoreGive(s_callback_lock);
        return -1;
    }
    s_emit = emit;
    s_user = user;
    __atomic_store_n(&s_touch_pending, 0, __ATOMIC_RELEASE);
    uint32_t epoch = (__atomic_load_n(&s_epoch, __ATOMIC_RELAXED) + 1U) & 0x7fffffffU;
    __atomic_store_n(&s_epoch, epoch, __ATOMIC_RELEASE);
    *out = &s_task;
    xSemaphoreGive(s_callback_lock);
    return 0;
}
static void keys_destroy(void *ctx) {
    if (ctx != &s_task)
        return;
    /* Board owns the polling worker so SET still provisions while MyBot is stopped.
     * The lock waits for the active callback before releasing its user data. */
    uint32_t epoch = __atomic_load_n(&s_epoch, __ATOMIC_ACQUIRE);
    if (epoch & 1U) {
        (void)__atomic_compare_exchange_n(&s_epoch, &epoch, (epoch + 1U) & 0x7fffffffU, false,
                                          __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
    }
    __atomic_store_n(&s_touch_pending, 0, __ATOMIC_RELEASE);
    xSemaphoreTake(s_callback_lock, portMAX_DELAY);
    if (s_emit) {
        s_emit = NULL;
        s_user = NULL;
    }
    xSemaphoreGive(s_callback_lock);
}
static const mybot_key_ops_t s_keys = {.init = keys_init, .destroy = keys_destroy};
const mybot_key_ops_t *mybot_korvo_s31_key_ops(void) {
    return &s_keys;
}
