/* SPDX-License-Identifier: MIT */
#include "host_platform.h"
#include "display/lvgl_view.h"
#include "input/korvo_s31_keys.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#define CHECK(test)                                                                                \
    do {                                                                                           \
        if (!(test)) {                                                                             \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #test);                   \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (0)

struct host_panel {
    bool open = false;
    bool initialized = false;
    void *buffers[2]{};
    rgb_callback_t callback = nullptr;
    void *user = nullptr;
} panel;
struct host_io {
    bool open = false;
} io;
struct host_touch {
    bool open = false;
} touch;
struct host_display {
    bool open = false;
    bool refresh = false;
    lv_display_flush_cb_t flush = nullptr;
    uint8_t *buffer = nullptr;
} display;
struct host_timer {
    bool open = false;
    lv_timer_cb_t callback = nullptr;
} timer, input_timer;
struct host_input {
    bool open = false;
    lv_indev_read_cb_t read = nullptr;
} input;

static bool port_open, port_locked, view_open, complete_frame = true;
static bool report_touch;
static unsigned ready_calls, frame_callbacks;
static std::string failure, cleanup_failure;
static std::set<void *> allocations;
static std::vector<std::string> events;
static mybot_lcd_content_t applied;
static std::vector<std::pair<bool, bool>> touch_samples;

static int step(const char *name) {
    events.emplace_back(name);
    if (failure == name) {
        failure.clear();
        return ESP_FAIL;
    }
    if (cleanup_failure == name) {
        cleanup_failure.clear();
        return ESP_FAIL;
    }
    return ESP_OK;
}

static void callback() {
    CHECK(panel.open && panel.callback && panel.user);
    ++frame_callbacks;
    (void)panel.callback(&panel, nullptr, panel.user);
}

extern "C" {
void mybot_korvo_s31_keys_touch_sample(bool valid, bool pressed) {
    CHECK(port_locked); // forwarding is non-blocking while the LVGL lock is owned
    touch_samples.emplace_back(valid, pressed);
}
const char *esp_err_to_name(esp_err_t result) {
    return result == ESP_OK ? "OK" : "FAIL";
}
void *heap_caps_aligned_calloc(size_t alignment, size_t count, size_t bytes, uint32_t caps) {
    CHECK(alignment >= 128 && caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
    CHECK(count == 1 && bytes == 800 * 480 * 2);
    if (step(allocations.empty() ? "fb0" : "fb1") != ESP_OK)
        return nullptr;
    void *result = nullptr;
    CHECK(posix_memalign(&result, alignment, bytes) == 0);
    std::memset(result, 0, bytes);
    CHECK(allocations.insert(result).second);
    return result;
}
void heap_caps_free(void *pointer) {
    if (!pointer)
        return;
    CHECK(!panel.open && !display.open);
    CHECK(allocations.erase(pointer) == 1);
    events.emplace_back("fb_free");
    std::free(pointer);
}
size_t esp_cache_get_line_size_by_addr(const void *pointer) {
    CHECK(allocations.count(const_cast<void *>(pointer)) == 1);
    return step("cache_alignment") == ESP_OK ? 64 : 256;
}
esp_err_t esp_lcd_new_rgb_panel(const esp_lcd_rgb_panel_config_t *config,
                                esp_lcd_panel_handle_t *out) {
    CHECK(!panel.open && config->num_fbs == 2 && config->user_fbs[0] && config->user_fbs[1]);
    CHECK(config->timings.h_res == 800 && config->timings.v_res == 480);
    CHECK(config->timings.pclk_hz == 26000000 && config->timings.flags.pclk_active_neg);
    CHECK(config->timings.hsync_pulse_width == 1 && config->timings.hsync_back_porch == 40 &&
          config->timings.hsync_front_porch == 20);
    CHECK(config->timings.vsync_pulse_width == 1 && config->timings.vsync_back_porch == 10 &&
          config->timings.vsync_front_porch == 5);
    CHECK(config->dma_burst_size == 128 && config->flags.fb_in_psram);
    CHECK(config->de_gpio_num == 43 && config->hsync_gpio_num == 44 &&
          config->vsync_gpio_num == 45 && config->pclk_gpio_num == 40 &&
          config->disp_gpio_num == -1);
    CHECK(config->in_color_format == LCD_COLOR_FMT_RGB565 &&
          config->out_color_format == LCD_COLOR_FMT_RGB565);
    for (int i = 0; i < ESP_LCD_RGB_BUS_WIDTH_MAX; ++i) {
        CHECK(config->data_gpio_nums[i] == (i < 12 ? 8 + i : (i < 16 ? 33 + i - 12 : -1)));
    }
    panel.open = true;
    panel.buffers[0] = config->user_fbs[0];
    panel.buffers[1] = config->user_fbs[1];
    *out = &panel; // fault injection includes a partially returned owner
    return step("panel_new");
}
esp_err_t esp_lcd_panel_reset(esp_lcd_panel_handle_t handle) {
    CHECK(handle == &panel && panel.open);
    return step("panel_reset");
}
esp_err_t esp_lcd_panel_init(esp_lcd_panel_handle_t handle) {
    CHECK(handle == &panel && panel.open);
    panel.initialized = true;
    return step("panel_init");
}
esp_err_t esp_lcd_panel_del(esp_lcd_panel_handle_t handle) {
    CHECK(handle == &panel && panel.open);
    if (panel.callback)
        callback(); // an ISR already running completes before del returns
    if (step("panel_del") != ESP_OK)
        return ESP_FAIL;
    panel = {};
    return ESP_OK;
}
esp_err_t esp_lcd_panel_draw_bitmap(esp_lcd_panel_handle_t handle, int x1, int y1, int x2, int y2,
                                    const void *pixels) {
    CHECK(handle == &panel && panel.open && display.open && port_locked);
    CHECK(x1 == 0 && y1 == 0 && x2 == 800 && y2 == 480);
    CHECK(pixels == panel.buffers[0] || pixels == panel.buffers[1]);
    if (step("draw") != ESP_OK)
        return ESP_FAIL;
    if (complete_frame)
        callback(); // completion before semaphore wait must be retained
    return ESP_OK;
}
esp_err_t
esp_lcd_rgb_panel_register_event_callbacks(esp_lcd_panel_handle_t handle,
                                           const esp_lcd_rgb_panel_event_callbacks_t *callbacks,
                                           void *opaque) {
    CHECK(handle == &panel && panel.open && callbacks->on_frame_buf_complete);
    panel.callback = callbacks->on_frame_buf_complete;
    panel.user = opaque;
    return step("register_callback");
}
i2c_master_bus_handle_t korvo_s31_hardware_i2c_bus() {
    return &io;
}
esp_err_t esp_lcd_new_panel_io_i2c(i2c_master_bus_handle_t bus,
                                   const esp_lcd_panel_io_i2c_config_t *config,
                                   esp_lcd_panel_io_handle_t *out) {
    CHECK(bus == &io && !io.open && config->scl_speed_hz == 400000 &&
          config->transaction_timeout_ms == 100);
    io.open = true;
    *out = &io;
    return step("touch_io_new");
}
esp_err_t esp_lcd_panel_io_del(esp_lcd_panel_io_handle_t handle) {
    CHECK(handle == &io && io.open && !touch.open && !input.open);
    if (step("touch_io_del") != ESP_OK)
        return ESP_FAIL;
    io.open = false;
    return ESP_OK;
}
esp_err_t esp_lcd_touch_new_i2c_gt1151(esp_lcd_panel_io_handle_t handle,
                                       const esp_lcd_touch_config_t *config,
                                       esp_lcd_touch_handle_t *out) {
    CHECK(handle == &io && io.open && !touch.open);
    CHECK(config->x_max == 800 && config->y_max == 480 && config->rst_gpio_num == -1 &&
          config->int_gpio_num == -1);
    touch.open = true;
    *out = &touch;
    return step("touch_new");
}
esp_err_t esp_lcd_touch_del(esp_lcd_touch_handle_t handle) {
    CHECK(handle == &touch && touch.open && !input.open);
    if (step("touch_del") != ESP_OK)
        return ESP_FAIL;
    touch.open = false;
    return ESP_OK;
}
esp_err_t esp_lcd_touch_read_data(esp_lcd_touch_handle_t handle) {
    CHECK(handle == &touch && touch.open);
    return step("touch_read");
}
esp_err_t esp_lcd_touch_get_data(esp_lcd_touch_handle_t handle, esp_lcd_touch_point_data_t *point,
                                 uint8_t *count, uint8_t max) {
    CHECK(handle == &touch && touch.open && max == 1);
    point->x = 350;
    point->y = 200;
    *count = report_touch ? 1 : 0;
    return step("touch_get");
}
SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *storage) {
    *storage = {true, false, 1};
    return storage;
}
SemaphoreHandle_t xSemaphoreCreateBinaryStatic(StaticSemaphore_t *storage) {
    *storage = {true, true, 0};
    return storage;
}
int xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t ticks) {
    CHECK(semaphore && semaphore->active && ticks != UINT32_MAX);
    if (!semaphore->count)
        return pdFALSE;
    --semaphore->count;
    return pdTRUE;
}
int xSemaphoreGive(SemaphoreHandle_t semaphore) {
    CHECK(semaphore && semaphore->active);
    semaphore->count = 1;
    return pdTRUE;
}
int xSemaphoreGiveFromISR(SemaphoreHandle_t semaphore, BaseType_t *wake) {
    *wake = pdFALSE;
    return xSemaphoreGive(semaphore);
}
void vSemaphoreDelete(SemaphoreHandle_t semaphore) {
    CHECK(semaphore && semaphore->active && !panel.open && !display.open);
    semaphore->active = false;
    events.emplace_back("sem_delete");
}
lv_display_t *lv_display_create(int width, int height) {
    CHECK(width == 800 && height == 480 && !display.open && port_locked);
    if (step("display_new") != ESP_OK)
        return nullptr;
    display.open = display.refresh = true;
    return &display;
}
void lv_display_delete(lv_display_t *handle) {
    CHECK(handle == &display && display.open && port_locked && !panel.open && !input.open &&
          !view_open);
    display = {};
    events.emplace_back("display_delete");
}
void lv_display_delete_refr_timer(lv_display_t *handle) {
    CHECK(handle == &display && display.open && port_locked);
    display.refresh = false;
}
void lv_display_set_color_format(lv_display_t *handle, int format) {
    CHECK(handle == &display && format == LV_COLOR_FORMAT_RGB565);
}
void lv_display_set_buffers(lv_display_t *handle, void *first, void *second, size_t bytes,
                            int mode) {
    CHECK(handle == &display && first == panel.buffers[1] && second == panel.buffers[0]);
    CHECK(bytes == 800 * 480 * 2 && mode == LV_DISPLAY_RENDER_MODE_FULL);
    display.buffer = static_cast<uint8_t *>(first);
}
void lv_display_set_flush_cb(lv_display_t *handle, lv_display_flush_cb_t cb) {
    CHECK(handle == &display);
    display.flush = cb;
}
bool lv_display_flush_is_last(lv_display_t *handle) {
    CHECK(handle == &display);
    return true;
}
void lv_display_flush_ready(lv_display_t *handle) {
    CHECK(handle == &display && display.open);
    ++ready_calls;
}
lv_indev_t *lv_indev_create() {
    CHECK(port_locked && !input.open);
    if (step("input_new") != ESP_OK)
        return nullptr;
    input.open = input_timer.open = true;
    return &input;
}
void lv_indev_delete(lv_indev_t *handle) {
    CHECK(handle == &input && input.open && port_locked);
    input = {};
    input_timer = {};
}
void lv_indev_set_type(lv_indev_t *handle, int type) {
    CHECK(handle == &input && type == LV_INDEV_TYPE_POINTER);
}
void lv_indev_set_display(lv_indev_t *handle, lv_display_t *out) {
    CHECK(handle == &input && out == &display);
}
void lv_indev_set_read_cb(lv_indev_t *handle, lv_indev_read_cb_t cb) {
    CHECK(handle == &input);
    input.read = cb;
}
lv_timer_t *lv_indev_get_read_timer(lv_indev_t *handle) {
    CHECK(handle == &input);
    return &input_timer;
}
lv_timer_t *lv_timer_create(lv_timer_cb_t cb, uint32_t period, void *) {
    CHECK(period == 50 && !timer.open);
    if (step("timer_new") != ESP_OK)
        return nullptr;
    timer.open = true;
    timer.callback = cb;
    return &timer;
}
void lv_timer_delete(lv_timer_t *handle) {
    CHECK(handle == &timer && timer.open && port_locked);
    timer = {};
}
void lv_timer_set_period(lv_timer_t *handle, uint32_t period) {
    CHECK(handle == &input_timer && period == 30);
}
esp_err_t lvgl_port_init(const lvgl_port_cfg_t *config) {
    CHECK(!port_open && config->task_affinity == 1 && config->task_priority == 1);
    CHECK(config->task_stack == 7168 &&
          config->task_stack_caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    port_open = true;
    return step("port_init");
}
esp_err_t lvgl_port_deinit() {
    CHECK(port_open && !port_locked && !display.open && !panel.open && !view_open);
    if (step("port_deinit") != ESP_OK)
        return ESP_FAIL;
    port_open = false;
    return ESP_OK;
}
bool lvgl_port_lock(uint32_t timeout) {
    CHECK(port_open && !port_locked && timeout > 0);
    if (step("port_lock") != ESP_OK)
        return false;
    port_locked = true;
    return true;
}
void lvgl_port_unlock() {
    CHECK(port_locked);
    port_locked = false;
}
esp_err_t lvgl_port_task_wake(int event, void *) {
    CHECK(event == LVGL_PORT_EVENT_USER && port_open);
    return ESP_OK;
}
int mybot_lvgl_view_create_sized(lv_display_t *handle, int width, int height) {
    CHECK(handle == &display && width == 800 && height == 480 && port_locked);
    view_open = true;
    return step("view_new");
}
void mybot_lvgl_view_update(const mybot_lcd_content_t *content) {
    CHECK(view_open && port_locked);
    applied = *content;
}
void mybot_lvgl_view_destroy() {
    CHECK(port_locked);
    view_open = false;
}
const mybot_lcd_ops_t *mybot_korvo_s31_lcd_ops();
}

static void clean() {
    CHECK(!port_locked && !port_open && !panel.open && !display.open && !input.open);
    CHECK(!touch.open && !io.open && !view_open && !timer.open && allocations.empty());
}

int main(int argc, char **argv) {
    CHECK(argc == 2);
    const std::string scenario = argv[1];
    const mybot_lcd_ops_t *ops = mybot_korvo_s31_lcd_ops();
    void *ctx = nullptr;
    if (scenario == "normal" || scenario == "timeout" || scenario == "draw_error" ||
        scenario == "stop_retry" || scenario == "touch_read" || scenario == "refcount" ||
        scenario == "cleanup_touch" || scenario == "cleanup_port") {
        CHECK(ops->init(&ctx) == 0 && ctx);
        if (scenario == "normal" || scenario == "touch_read") {
            mybot_lcd_content_t content{};
            content.screen = MYBOT_LCD_SCREEN_PAIR_CODE;
            std::strcpy(content.pair_code, "123456");
            CHECK(ops->render(ctx, &content) == 0);
            CHECK(lvgl_port_lock(100));
            timer.callback(&timer);
            CHECK(applied.screen == content.screen && !std::strcmp(applied.pair_code, "123456"));
            display.buffer[0] = 0x34;
            display.buffer[1] = 0x12;
            const lv_area_t area{0, 0, 799, 479};
            display.flush(&display, &area, display.buffer);
            CHECK(display.buffer[0] == 0x34 && display.buffer[1] == 0x12); // no SPI swap
            CHECK(frame_callbacks == 1 && ready_calls == 1);
            lv_indev_data_t data{};
            report_touch = true;
            input.read(&input, &data);
            CHECK(data.state == LV_INDEV_STATE_PRESSED && data.point.x == 350 &&
                  data.point.y == 200);
            CHECK(touch_samples.back() == std::make_pair(true, true));
            if (scenario == "touch_read") {
                failure = "touch_read";
                input.read(&input, &data);
                CHECK(data.state == LV_INDEV_STATE_RELEASED);
                CHECK(touch_samples.back() == std::make_pair(false, false));
                input.read(&input, &data);
                CHECK(data.state == LV_INDEV_STATE_PRESSED);
                CHECK(touch_samples.back() == std::make_pair(true, true));
                failure = "touch_get";
                input.read(&input, &data);
                CHECK(data.state == LV_INDEV_STATE_RELEASED);
                CHECK(touch_samples.back() == std::make_pair(false, false));
            }
            report_touch = false;
            input.read(&input, &data);
            CHECK(data.state == LV_INDEV_STATE_RELEASED);
            CHECK(touch_samples.back() == std::make_pair(true, false));
            lvgl_port_unlock();
        } else if (scenario == "refcount") {
            void *other = nullptr;
            CHECK(ops->init(&other) == 0 && other == ctx);
            ops->destroy(other);
            CHECK(panel.open && display.open && allocations.size() == 2);
        } else if (scenario == "timeout" || scenario == "draw_error" || scenario == "stop_retry") {
            complete_frame = false;
            if (scenario == "draw_error")
                failure = "draw";
            if (scenario == "stop_retry")
                cleanup_failure = "panel_del";
            CHECK(lvgl_port_lock(100));
            const lv_area_t area{0, 0, 799, 479};
            display.flush(&display, &area, display.buffer);
            CHECK(!display.refresh && allocations.size() == 2 && display.open);
            CHECK(panel.open == (scenario == "stop_retry"));
            CHECK(ready_calls == (scenario == "stop_retry" ? 0U : 1U));
            lvgl_port_unlock();
            mybot_lcd_content_t content{};
            CHECK(ops->render(ctx, &content) < 0);
        } else if (scenario == "cleanup_touch") {
            cleanup_failure = "touch_del";
        } else if (scenario == "cleanup_port") {
            cleanup_failure = "port_deinit";
        }
        ops->destroy(ctx);
        CHECK(!touch_samples.empty() && touch_samples.back() == std::make_pair(false, false));
        ops->destroy(ctx); // deferred close retry and idempotent release
        clean();
    } else {
        failure = scenario;
        CHECK(ops->init(&ctx) < 0 && !ctx);
        clean();
    }
    // Every scenario must permit a complete fresh lifecycle afterwards.
    CHECK(ops->init(&ctx) == 0 && ctx);
    ops->destroy(ctx);
    clean();
    std::printf("PASS korvo_s31_display %s\n", argv[1]);
}
