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

#include <string.h>

enum { KEY_NONE = -1, KEY_UP, KEY_DOWN, KEY_PLAY, KEY_SET };
static adc_oneshot_unit_handle_t s_adc;
static TaskHandle_t s_task;
static StaticSemaphore_t s_callback_storage;
static SemaphoreHandle_t s_callback_lock;
static mybot_key_event_handler_t s_emit;
static void *s_user;
/* A 23-bit SDK epoch, five state bits and four event bits fit one RV32 atomic.
 * Odd epochs mean attached. Buttons cannot survive a detach or state change. */
#define EPOCH_MASK 0x7fffffU
static uint32_t s_epoch;
static uint32_t s_touch_pending;
/* One copied GUI command. 0=empty, 1=producer owns, 2=ready, 3=consumer owns.
 * Attach/detach only invalidates the epoch; it must never overwrite a producer. */
static uint32_t s_command_state;
static uint32_t s_command_gesture;
static mybot_ui_command_t s_command;
/* These gesture fields belong exclusively to the LVGL owner. */
static bool s_touch_released;
static uint32_t s_touch_gesture;
static uint32_t s_touch_click;

static bool event_allowed(mybot_key_event_t event, mybot_state_t state) {
    bool idle = state == MYBOT_STATE_READY || state == MYBOT_STATE_TRANSLATION_ERROR;
    switch (event) {
    case MYBOT_KEY_EVENT_CONVERSATION_START:
    case MYBOT_KEY_EVENT_SOURCE_LANGUAGE_NEXT:
    case MYBOT_KEY_EVENT_TARGET_LANGUAGE_NEXT:
        return idle;
    case MYBOT_KEY_EVENT_CONVERSATION_STOP:
        return state == MYBOT_STATE_IN_CONVERSATION || state == MYBOT_STATE_TRANSLATION_STARTING ||
               state == MYBOT_STATE_CLEANUP_PENDING;
    case MYBOT_KEY_EVENT_VOLUME_UP:
    case MYBOT_KEY_EVENT_VOLUME_DOWN:
        return idle || state == MYBOT_STATE_IN_CONVERSATION ||
               state == MYBOT_STATE_TRANSLATION_STARTING;
    default:
        return false;
    }
}

static bool start_completed(mybot_state_t before, mybot_state_t after) {
    return before == MYBOT_STATE_TRANSLATION_STARTING &&
           (after == MYBOT_STATE_IN_CONVERSATION || after == MYBOT_STATE_CLEANUP_PENDING);
}

static bool event_matches_gesture(mybot_key_event_t event, uint32_t gesture, mybot_state_t state) {
    mybot_state_t pressed_state = (mybot_state_t)((gesture >> 4) & 31U);
    return event_allowed(event, state) &&
           (pressed_state == state ||
            (event == MYBOT_KEY_EVENT_CONVERSATION_STOP && start_completed(pressed_state, state)));
}

void mybot_korvo_s31_keys_touch_sample(bool valid, bool pressed) {
    s_touch_click = 0;
    if (!valid) {
        s_touch_released = false;
        s_touch_gesture = 0;
        return;
    }
    uint32_t epoch = __atomic_load_n(&s_epoch, __ATOMIC_ACQUIRE);
    if (pressed) {
        if (s_touch_released) {
            s_touch_released = false;
            mybot_state_t state = mybot_get_state();
            s_touch_gesture = (epoch & 1U) ? (epoch << 9) | ((uint32_t)state << 4) : 0;
        }
        return;
    }
    s_touch_released = true;
    uint32_t gesture = s_touch_gesture;
    s_touch_gesture = 0;
    mybot_state_t state = mybot_get_state();
    mybot_state_t pressed_state = (mybot_state_t)((gesture >> 4) & 31U);
    if (gesture && (gesture >> 9) == epoch &&
        (pressed_state == state || start_completed(pressed_state, state)))
        s_touch_click = gesture;
}

void mybot_korvo_s31_keys_ui_event(mybot_key_event_t event) {
    uint32_t click = s_touch_click;
    s_touch_click = 0;
    if (!click || (unsigned)event >= 15U ||
        (click >> 9) != __atomic_load_n(&s_epoch, __ATOMIC_ACQUIRE) ||
        !event_matches_gesture(event, click, mybot_get_state()))
        return;
    uint32_t empty = 0;
    (void)__atomic_compare_exchange_n(&s_touch_pending, &empty, click | ((unsigned)event + 1U),
                                      false, __ATOMIC_RELEASE, __ATOMIC_RELAXED);
}

static bool command_allowed(const mybot_ui_command_t *command, mybot_state_t state) {
    if (!command)
        return false;
    switch (command->type) {
    case MYBOT_UI_SET_LANGUAGES:
        return event_allowed(MYBOT_KEY_EVENT_SOURCE_LANGUAGE_NEXT, state) &&
               memchr(command->source_language, '\0', sizeof(command->source_language)) &&
               memchr(command->target_language, '\0', sizeof(command->target_language));
    case MYBOT_UI_SET_VOLUME:
        return event_allowed(MYBOT_KEY_EVENT_VOLUME_UP, state) && command->value >= 0 &&
               command->value <= 100;
    case MYBOT_UI_SET_PAUSED:
        return state == MYBOT_STATE_IN_CONVERSATION && (command->value == 0 || command->value == 1);
    default:
        return false;
    }
}

void mybot_korvo_s31_keys_ui_command(const mybot_ui_command_t *command) {
    uint32_t click = s_touch_click;
    s_touch_click = 0;
    if (!click || (click >> 9) != __atomic_load_n(&s_epoch, __ATOMIC_ACQUIRE) ||
        !command_allowed(command, mybot_get_state()))
        return;
    uint32_t empty = 0;
    if (!__atomic_compare_exchange_n(&s_command_state, &empty, 1, false, __ATOMIC_ACQUIRE,
                                     __ATOMIC_RELAXED))
        return;
    s_command = *command;
    s_command_gesture = click;
    __atomic_store_n(&s_command_state, 2, __ATOMIC_RELEASE);
}

static void dispatch_ui_command(void) {
    if (__atomic_load_n(&s_command_state, __ATOMIC_ACQUIRE) != 2)
        return;
    xSemaphoreTake(s_callback_lock, portMAX_DELAY);
    uint32_t ready = 2;
    if (__atomic_compare_exchange_n(&s_command_state, &ready, 3, false, __ATOMIC_ACQUIRE,
                                    __ATOMIC_RELAXED)) {
        mybot_ui_command_t command = s_command;
        uint32_t gesture = s_command_gesture;
        __atomic_store_n(&s_command_state, 0, __ATOMIC_RELEASE);
        mybot_state_t state = mybot_get_state();
        if (s_emit && (gesture >> 9) == __atomic_load_n(&s_epoch, __ATOMIC_ACQUIRE) &&
            ((gesture >> 4) & 31U) == (uint32_t)state && command_allowed(&command, state))
            (void)mybot_ui_submit(&command);
    }
    xSemaphoreGive(s_callback_lock);
}

static void dispatch_touch(void) {
    if (!__atomic_load_n(&s_touch_pending, __ATOMIC_ACQUIRE))
        return;
    xSemaphoreTake(s_callback_lock, portMAX_DELAY);
    uint32_t pending = __atomic_exchange_n(&s_touch_pending, 0, __ATOMIC_ACQ_REL);
    mybot_key_event_t event = (mybot_key_event_t)((pending & 15U) - 1U);
    mybot_state_t state = mybot_get_state();
    if (pending && s_emit && (pending >> 9) == __atomic_load_n(&s_epoch, __ATOMIC_ACQUIRE) &&
        event_matches_gesture(event, pending, state))
        s_emit(event, s_user);
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
            if (event_allowed(MYBOT_KEY_EVENT_CONVERSATION_START, state))
                s_emit(MYBOT_KEY_EVENT_CONVERSATION_START, s_user);
            else if (event_allowed(MYBOT_KEY_EVENT_CONVERSATION_STOP, state))
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
        dispatch_ui_command();
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
    uint32_t epoch = (__atomic_load_n(&s_epoch, __ATOMIC_RELAXED) + 1U) & EPOCH_MASK;
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
        (void)__atomic_compare_exchange_n(&s_epoch, &epoch, (epoch + 1U) & EPOCH_MASK, false,
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
