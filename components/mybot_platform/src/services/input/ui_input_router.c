/* SPDX-License-Identifier: Apache-2.0 */
#include "input/ui_input.h"

#include "mybot_platform/board.h"
#include "mybot_platform/factory_reset.h"

#include <mybot/mybot.h>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <string.h>

#define EPOCH_MASK 0x7fffffU
#define DEBOUNCE_SAMPLES 3
#define PROVISION_HOLD_MS 3000
#define FACTORY_RESET_HOLD_MS 10000
#define TASK_STACK_BYTES 3072

static bool (*s_pressed)(void);
static TaskHandle_t s_task;
static StaticSemaphore_t s_callback_storage;
static SemaphoreHandle_t s_callback_lock;
static mybot_key_event_handler_t s_emit;
static void *s_user;
/* Odd epochs mean attached; state and event bits are validated with the gesture. */
static uint32_t s_epoch;
static uint32_t s_touch_pending;
/* One copied GUI command: 0 empty, 1 producer, 2 ready, 3 consumer. */
/* These gesture fields and the contact state belong exclusively to the LVGL owner. */
typedef enum {
    TOUCH_UNARMED,
    TOUCH_ARMED,
    TOUCH_DOWN,
} touch_contact_t;
static touch_contact_t s_touch_contact;
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

mybot_ui_touch_sample_t mybot_ui_input_touch_sample(bool valid, bool pressed) {
    s_touch_click = 0;
    if (!valid) {
        s_touch_contact = TOUCH_UNARMED;
        s_touch_gesture = 0;
        return MYBOT_UI_TOUCH_INVALID;
    }
    uint32_t epoch = __atomic_load_n(&s_epoch, __ATOMIC_ACQUIRE);
    if (pressed) {
        if (s_touch_contact != TOUCH_ARMED) {
            return s_touch_contact == TOUCH_DOWN ? MYBOT_UI_TOUCH_PRESS : MYBOT_UI_TOUCH_NONE;
        }
        s_touch_contact = TOUCH_DOWN;
        mybot_state_t state = mybot_get_state();
        s_touch_gesture = (epoch & 1U) ? (epoch << 9) | ((uint32_t)state << 4) : 0;
        return MYBOT_UI_TOUCH_PRESS;
    }
    s_touch_contact = TOUCH_ARMED;
    uint32_t gesture = s_touch_gesture;
    s_touch_gesture = 0;
    mybot_state_t state = mybot_get_state();
    mybot_state_t pressed_state = (mybot_state_t)((gesture >> 4) & 31U);
    if (gesture && (gesture >> 9) == epoch &&
        (pressed_state == state || start_completed(pressed_state, state)))
        s_touch_click = gesture;
    return MYBOT_UI_TOUCH_NONE;
}

void mybot_ui_input_event(mybot_key_event_t event) {
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

int mybot_ui_input_command(const mybot_ui_command_t *command) {
    uint32_t click = s_touch_click;
    s_touch_click = 0;
    if (!click || (click >> 9) != __atomic_load_n(&s_epoch, __ATOMIC_ACQUIRE) ||
        !command_allowed(command, mybot_get_state()))
        return -1;
    return mybot_ui_submit(command);
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

static void key_task(void *arg) {
    (void)arg;
    bool candidate = false, stable = false, released = false;
    unsigned samples = 0;
    int64_t pressed_at = 0;
    bool provisioning_sent = false;
    bool factory_reset_sent = false;
    for (;;) {
        bool current = s_pressed();
        dispatch_touch();
        int64_t now = esp_timer_get_time() / 1000;
        if (current != candidate) {
            candidate = current;
            samples = 1;
        } else if (samples < DEBOUNCE_SAMPLES) {
            ++samples;
        }
        if (samples == DEBOUNCE_SAMPLES) {
            if (!candidate)
                released = true;
            if (stable != candidate) {
                stable = candidate && released;
                pressed_at = now;
                provisioning_sent = false;
                factory_reset_sent = false;
            }
        }
        if (stable && !provisioning_sent && now - pressed_at >= PROVISION_HOLD_MS) {
            provisioning_sent = true;
            mybot_board_request_wifi_provisioning();
        }
        if (stable && provisioning_sent && !factory_reset_sent &&
            now - pressed_at >= FACTORY_RESET_HOLD_MS) {
            factory_reset_sent = true;
            mybot_factory_reset_request();
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

int mybot_ui_input_start(int (*prepare)(void), bool (*pressed)(void)) {
    if (s_task)
        return 0;
    if (!prepare || !pressed)
        return -1;
    s_pressed = pressed;
    if (!s_callback_lock)
        s_callback_lock = xSemaphoreCreateMutexStatic(&s_callback_storage);
    if (!s_callback_lock || prepare() < 0) {
        s_pressed = NULL;
        return -1;
    }
    if (xTaskCreateWithCaps(key_task, "mosaico_input", TASK_STACK_BYTES, NULL, 4, &s_task,
                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        s_pressed = NULL;
        return -1;
    }
    return 0;
}

int mybot_ui_input_attach(void **out_ctx, mybot_key_event_handler_t emit, void *user_data) {
    if (!out_ctx)
        return -1;
    *out_ctx = NULL;
    if (!emit || !s_task)
        return -1;
    xSemaphoreTake(s_callback_lock, portMAX_DELAY);
    if (s_emit) {
        xSemaphoreGive(s_callback_lock);
        return -1;
    }
    s_emit = emit;
    s_user = user_data;
    __atomic_store_n(&s_touch_pending, 0, __ATOMIC_RELEASE);
    uint32_t epoch = (__atomic_load_n(&s_epoch, __ATOMIC_RELAXED) + 1U) & EPOCH_MASK;
    __atomic_store_n(&s_epoch, epoch, __ATOMIC_RELEASE);
    *out_ctx = &s_task;
    xSemaphoreGive(s_callback_lock);
    return 0;
}

void mybot_ui_input_destroy(void *ctx) {
    if (ctx != &s_task)
        return;
    uint32_t epoch = __atomic_load_n(&s_epoch, __ATOMIC_ACQUIRE);
    if (epoch & 1U) {
        (void)__atomic_compare_exchange_n(&s_epoch, &epoch, (epoch + 1U) & EPOCH_MASK, false,
                                          __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
    }
    __atomic_store_n(&s_touch_pending, 0, __ATOMIC_RELEASE);
    xSemaphoreTake(s_callback_lock, portMAX_DELAY);
    s_emit = NULL;
    s_user = NULL;
    xSemaphoreGive(s_callback_lock);
}
