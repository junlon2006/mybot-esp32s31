/* SPDX-License-Identifier: MIT */
#include "board_config.h"
#include "display/korvo_s31_rgb_panel.h"
#include "display/korvo_s31_touch.h"
#include "display/lvgl_view.h"
#include "input/korvo_s31_keys.h"

#include <mybot/platform/mybot_lcd.h>

#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <cstring>

#define TAG "korvo_s31_lvgl"
#define UI_LOCK_TIMEOUT_MS 1500
#define UI_FRAME_TIMEOUT_MS 250
#define UI_APPLY_PERIOD_MS 50
#define UI_TOUCH_PERIOD_MS 30
#define UI_STACK_BYTES 7168

namespace {
struct Context {
    mybot_korvo_s31_rgb_panel_t panel{};
    mybot_korvo_s31_touch_t touch{};
    lv_display_t *display = nullptr;
    lv_indev_t *input = nullptr;
    lv_timer_t *apply_timer = nullptr;
    mybot_lcd_content_t latest{};
    bool pending = false;
    bool accepting = false;
    bool port_started = false;
    bool cleanup_pending = false;
    unsigned users = 0;
    uint32_t failed = 0;
    StaticSemaphore_t frame_storage{};
    SemaphoreHandle_t frame_done = nullptr;
};

/* ISR state stays in internal BSS; only draw buffers, widgets and UI stack use PSRAM. */
Context context;
StaticSemaphore_t lifecycle_storage;
SemaphoreHandle_t lifecycle_mutex;
portMUX_TYPE lifecycle_init_lock = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE snapshot_lock = portMUX_INITIALIZER_UNLOCKED;

bool lock_lifecycle() {
    portENTER_CRITICAL(&lifecycle_init_lock);
    if (!lifecycle_mutex) {
        lifecycle_mutex = xSemaphoreCreateMutexStatic(&lifecycle_storage);
    }
    SemaphoreHandle_t mutex = lifecycle_mutex;
    portEXIT_CRITICAL(&lifecycle_init_lock);
    return mutex && xSemaphoreTake(mutex, pdMS_TO_TICKS(UI_LOCK_TIMEOUT_MS)) == pdTRUE;
}

void unlock_lifecycle() {
    xSemaphoreGive(lifecycle_mutex);
}

bool IRAM_ATTR frame_complete(esp_lcd_panel_handle_t panel,
                              const esp_lcd_rgb_panel_event_data_t *event, void *opaque) {
    (void)panel;
    (void)event;
    auto *ctx = static_cast<Context *>(opaque);
    BaseType_t wake = pdFALSE;
    xSemaphoreGiveFromISR(ctx->frame_done, &wake);
    return wake == pdTRUE;
}

void fail_display() {
    __atomic_store_n(&context.failed, 1, __ATOMIC_RELEASE);
    /* A timed-out swap must not let LVGL reuse a buffer still owned by GDMA.
     * Stop the hardware first, retaining both user framebuffers until LVGL is
     * removed. Even a failed stop retains all buffers and defers cleanup. */
    if (mybot_korvo_s31_rgb_panel_stop(&context.panel) < 0) {
        ESP_LOGE(TAG, "event=lcd action=stop result=pending buffers=retained");
    }
    lv_display_delete_refr_timer(context.display);
}

void flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels) {
    (void)area;
    if (__atomic_load_n(&context.failed, __ATOMIC_ACQUIRE)) {
        if (!context.panel.handle) {
            lv_display_flush_ready(display);
        }
        return;
    }
    if (!lv_display_flush_is_last(display)) {
        lv_display_flush_ready(display);
        return;
    }
    /* Full refresh uses the two panel buffers directly. RGB consumes native
     * RGB565, with no SPI byte swap. S31's GDMA link-switch event says when the
     * previous frame can be rendered again; VSYNC alone does not guarantee it. */
    (void)xSemaphoreTake(context.frame_done, 0);
    esp_err_t result = esp_lcd_panel_draw_bitmap(context.panel.handle, 0, 0, MYBOT_DISPLAY_WIDTH,
                                                 MYBOT_DISPLAY_HEIGHT, pixels);
    if (result == ESP_OK &&
        xSemaphoreTake(context.frame_done, pdMS_TO_TICKS(UI_FRAME_TIMEOUT_MS)) != pdTRUE) {
        result = ESP_ERR_TIMEOUT;
    }
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "event=lcd action=flush result=error error=%s timeout_ms=%d",
                 esp_err_to_name(result), UI_FRAME_TIMEOUT_MS);
        fail_display();
        /* If the hardware stop failed, leave the buffer pending. Teardown will
         * retry stopping GDMA before acknowledging/releasing this flush. */
        if (context.panel.handle) {
            return;
        }
    }
    lv_display_flush_ready(display);
}

void read_touch(lv_indev_t *input, lv_indev_data_t *data) {
    (void)input;
    uint16_t x = 0, y = 0;
    bool pressed = false;
    data->state = LV_INDEV_STATE_RELEASED;
    bool valid = !__atomic_load_n(&context.failed, __ATOMIC_ACQUIRE) &&
                 mybot_korvo_s31_touch_read(&context.touch, &x, &y, &pressed) == 0;
    mybot_korvo_s31_keys_touch_sample(valid, pressed);
    if (valid && pressed) {
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PRESSED;
    }
}

void apply_snapshot(lv_timer_t *timer) {
    (void)timer;
    if (__atomic_load_n(&context.failed, __ATOMIC_ACQUIRE)) {
        return;
    }
    mybot_lcd_content_t snapshot{};
    portENTER_CRITICAL(&snapshot_lock);
    bool update = context.accepting && context.pending;
    if (update) {
        snapshot = context.latest;
        context.pending = false;
    }
    portEXIT_CRITICAL(&snapshot_lock);
    if (update) {
        mybot_lvgl_view_update(&snapshot);
    }
}

int release_display() {
    context.cleanup_pending = true;
    portENTER_CRITICAL(&snapshot_lock);
    context.accepting = false;
    context.pending = false;
    portEXIT_CRITICAL(&snapshot_lock);
    if (context.display) {
        if (!lvgl_port_lock(UI_LOCK_TIMEOUT_MS)) {
            return -1;
        }
        lv_display_delete_refr_timer(context.display);
        if (context.apply_timer) {
            lv_timer_delete(context.apply_timer);
            context.apply_timer = nullptr;
        }
        if (context.input) {
            mybot_korvo_s31_keys_touch_sample(false, false);
            lv_indev_delete(context.input);
            context.input = nullptr;
        }
        /* Stop/join GDMA and its ISR before freeing the display, semaphore or
         * draw buffers. The shared view remains alive if stopping must retry. */
        if (mybot_korvo_s31_rgb_panel_stop(&context.panel) < 0) {
            lvgl_port_unlock();
            return -1;
        }
        lv_display_flush_ready(context.display);
        mybot_lvgl_view_destroy();
        lv_display_delete(context.display);
        context.display = nullptr;
        lvgl_port_unlock();
    }
    if (mybot_korvo_s31_rgb_panel_close(&context.panel) < 0 ||
        mybot_korvo_s31_touch_close(&context.touch) < 0) {
        return -1;
    }
    if (context.frame_done) {
        vSemaphoreDelete(context.frame_done);
        context.frame_done = nullptr;
    }
    if (context.port_started) {
        if (lvgl_port_deinit() != ESP_OK) {
            return -1;
        }
        context.port_started = false;
    }
    context.cleanup_pending = false;
    context.users = 0;
    __atomic_store_n(&context.failed, 0, __ATOMIC_RELEASE);
    return 0;
}

int initialize(void **out_ctx) {
    if (!out_ctx || !lock_lifecycle()) {
        return -1;
    }
    *out_ctx = nullptr;
    if (context.cleanup_pending && release_display() < 0) {
        unlock_lifecycle();
        return -1;
    }
    if (context.users) {
        if (__atomic_load_n(&context.failed, __ATOMIC_ACQUIRE)) {
            unlock_lifecycle();
            return -1;
        }
        ++context.users;
        *out_ctx = &context;
        unlock_lifecycle();
        return 0;
    }

    int result = -1;
    lvgl_port_cfg_t port_config = ESP_LVGL_PORT_INIT_CONFIG();
    port_config.task_priority = 1;
    port_config.task_affinity = 1;
    port_config.task_stack = UI_STACK_BYTES;
    port_config.task_stack_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    port_config.task_max_sleep_ms = UI_APPLY_PERIOD_MS;
    esp_lcd_rgb_panel_event_callbacks_t callbacks{};
    callbacks.on_frame_buf_complete = frame_complete;
    context.frame_done = xSemaphoreCreateBinaryStatic(&context.frame_storage);
    if (!context.frame_done || mybot_korvo_s31_rgb_panel_open(&context.panel) < 0 ||
        mybot_korvo_s31_touch_open(&context.touch) < 0 ||
        esp_lcd_rgb_panel_register_event_callbacks(context.panel.handle, &callbacks, &context) !=
            ESP_OK) {
        goto done;
    }
    /* Track even failed port init, whose task cleanup can require a retry. */
    context.port_started = true;
    if (lvgl_port_init(&port_config) != ESP_OK || !lvgl_port_lock(UI_LOCK_TIMEOUT_MS)) {
        goto done;
    }
    context.display = lv_display_create(MYBOT_DISPLAY_WIDTH, MYBOT_DISPLAY_HEIGHT);
    if (!context.display) {
        lvgl_port_unlock();
        goto done;
    }
    lv_display_set_color_format(context.display, LV_COLOR_FORMAT_RGB565);
    /* Buffer zero is initially scanned by RGB; render the first frame into one. */
    lv_display_set_buffers(context.display, context.panel.framebuffers[1],
                           context.panel.framebuffers[0], context.panel.framebuffer_bytes,
                           LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(context.display, flush);
    if (mybot_lvgl_view_create_sized(context.display, MYBOT_DISPLAY_WIDTH, MYBOT_DISPLAY_HEIGHT) <
        0) {
        lvgl_port_unlock();
        goto done;
    }
    context.input = lv_indev_create();
    if (!context.input) {
        lvgl_port_unlock();
        goto done;
    }
    lv_indev_set_type(context.input, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(context.input, context.display);
    lv_indev_set_read_cb(context.input, read_touch);
    lv_timer_set_period(lv_indev_get_read_timer(context.input), UI_TOUCH_PERIOD_MS);
    context.apply_timer = lv_timer_create(apply_snapshot, UI_APPLY_PERIOD_MS, nullptr);
    if (!context.apply_timer) {
        lvgl_port_unlock();
        goto done;
    }
    portENTER_CRITICAL(&snapshot_lock);
    context.latest = {};
    context.latest.screen = MYBOT_LCD_SCREEN_STARTING;
    context.pending = true;
    context.accepting = true;
    portEXIT_CRITICAL(&snapshot_lock);
    context.users = 1;
    lvgl_port_unlock();
    *out_ctx = &context;
    result = 0;
    ESP_LOGI(TAG,
             "event=lcd action=initialize result=ok width=%d height=%d framebuffers=2 "
             "objects=psram ui_stack=psram ui_stack_bytes=%d touch=gt1151 rotation=0",
             MYBOT_DISPLAY_WIDTH, MYBOT_DISPLAY_HEIGHT, UI_STACK_BYTES);
done:
    if (result < 0) {
        ESP_LOGE(TAG, "event=lcd action=initialize result=error");
        if (release_display() < 0) {
            ESP_LOGE(TAG, "event=lcd action=cleanup result=pending");
        }
    }
    unlock_lifecycle();
    return result;
}

int render(void *opaque, const mybot_lcd_content_t *content) {
    if (opaque != &context || !content || content->screen < MYBOT_LCD_SCREEN_STARTING ||
        content->screen >= MYBOT_LCD_SCREEN_COUNT) {
        return -1;
    }
    mybot_lcd_content_t snapshot{};
    snapshot.screen = content->screen;
    if (snapshot.screen == MYBOT_LCD_SCREEN_IN_CONVERSATION) {
        snapshot.indicators = content->indicators;
    }
    if (snapshot.screen == MYBOT_LCD_SCREEN_PAIR_CODE) {
        size_t length = strnlen(content->pair_code, sizeof(content->pair_code));
        if (!length || length >= sizeof(content->pair_code)) {
            return -1;
        }
        for (size_t i = 0; i < length; ++i) {
            if (content->pair_code[i] < ' ' || content->pair_code[i] > '~') {
                return -1;
            }
        }
        memcpy(snapshot.pair_code, content->pair_code, length);
    }
    if (!lock_lifecycle()) {
        return -1;
    }
    int result = -1;
    portENTER_CRITICAL(&snapshot_lock);
    if (context.accepting && context.users && !__atomic_load_n(&context.failed, __ATOMIC_ACQUIRE)) {
        if (memcmp(&snapshot, &context.latest, sizeof(snapshot)) != 0) {
            context.latest = snapshot;
            context.pending = true;
        }
        result = 0;
    }
    portEXIT_CRITICAL(&snapshot_lock);
    if (result == 0 && lvgl_port_task_wake(LVGL_PORT_EVENT_USER, nullptr) != ESP_OK) {
        result = -1;
    }
    unlock_lifecycle();
    return result;
}

void destroy(void *opaque) {
    if (opaque != &context || !lock_lifecycle()) {
        return;
    }
    if (context.users > 1) {
        --context.users;
    } else if (context.users || context.cleanup_pending) {
        if (release_display() < 0) {
            ESP_LOGE(TAG, "event=lcd action=destroy result=pending");
        }
    }
    unlock_lifecycle();
}

const mybot_lcd_ops_t ops = {initialize, render, destroy};
} // namespace

extern "C" const mybot_lcd_ops_t *mybot_korvo_s31_lcd_ops(void) {
    return &ops;
}
