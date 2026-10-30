/* SPDX-License-Identifier: MIT */
#ifndef KORVO_S31_DISPLAY_HOST_PLATFORM_H_
#define KORVO_S31_DISPLAY_HOST_PLATFORM_H_
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_TIMEOUT 0x107
#define IRAM_ATTR
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
const char *esp_err_to_name(esp_err_t error);
#define MALLOC_CAP_8BIT 1U
#define MALLOC_CAP_SPIRAM 2U
#define MALLOC_CAP_DMA 4U
void *heap_caps_aligned_calloc(size_t alignment, size_t count, size_t size, uint32_t caps);
void heap_caps_free(void *pointer);
size_t esp_cache_get_line_size_by_addr(const void *pointer);
typedef int gpio_num_t;
#define GPIO_NUM_NC -1
#define LCD_CLK_SRC_DEFAULT 0
#define LCD_COLOR_FMT_RGB565 1
#define ESP_LCD_RGB_BUS_WIDTH_MAX 24
typedef struct host_panel *esp_lcd_panel_handle_t;
typedef struct host_io *esp_lcd_panel_io_handle_t;
typedef struct {
    int unused;
} esp_lcd_rgb_panel_event_data_t;
typedef bool (*rgb_callback_t)(esp_lcd_panel_handle_t, const esp_lcd_rgb_panel_event_data_t *,
                               void *);
typedef struct {
    rgb_callback_t on_frame_buf_complete;
} esp_lcd_rgb_panel_event_callbacks_t;
typedef struct {
    int clk_src;
    struct {
        int pclk_hz, h_res, v_res;
        int hsync_pulse_width, hsync_back_porch, hsync_front_porch;
        int vsync_pulse_width, vsync_back_porch, vsync_front_porch;
        struct {
            bool pclk_active_neg;
        } flags;
    } timings;
    size_t data_width;
    int in_color_format, out_color_format;
    size_t num_fbs;
    void *user_fbs[3];
    size_t dma_burst_size;
    gpio_num_t hsync_gpio_num, vsync_gpio_num, de_gpio_num, pclk_gpio_num, disp_gpio_num;
    gpio_num_t data_gpio_nums[ESP_LCD_RGB_BUS_WIDTH_MAX];
    struct {
        bool fb_in_psram;
    } flags;
} esp_lcd_rgb_panel_config_t;
esp_err_t esp_lcd_new_rgb_panel(const esp_lcd_rgb_panel_config_t *, esp_lcd_panel_handle_t *);
esp_err_t esp_lcd_panel_reset(esp_lcd_panel_handle_t);
esp_err_t esp_lcd_panel_init(esp_lcd_panel_handle_t);
esp_err_t esp_lcd_panel_del(esp_lcd_panel_handle_t);
esp_err_t esp_lcd_panel_draw_bitmap(esp_lcd_panel_handle_t, int, int, int, int, const void *);
esp_err_t esp_lcd_rgb_panel_register_event_callbacks(esp_lcd_panel_handle_t,
                                                     const esp_lcd_rgb_panel_event_callbacks_t *,
                                                     void *);
typedef void *i2c_master_bus_handle_t;
i2c_master_bus_handle_t korvo_s31_hardware_i2c_bus(void);
typedef struct host_touch *esp_lcd_touch_handle_t;
typedef struct {
    uint16_t x_max, y_max;
    gpio_num_t rst_gpio_num, int_gpio_num;
} esp_lcd_touch_config_t;
typedef struct {
    unsigned scl_speed_hz;
    int transaction_timeout_ms;
} esp_lcd_panel_io_i2c_config_t;
#define ESP_LCD_TOUCH_IO_I2C_GT1151_CONFIG()                                                       \
    { .scl_speed_hz = 100000 }
typedef struct {
    uint16_t x, y;
} esp_lcd_touch_point_data_t;
esp_err_t esp_lcd_new_panel_io_i2c(i2c_master_bus_handle_t, const esp_lcd_panel_io_i2c_config_t *,
                                   esp_lcd_panel_io_handle_t *);
esp_err_t esp_lcd_panel_io_del(esp_lcd_panel_io_handle_t);
esp_err_t esp_lcd_touch_new_i2c_gt1151(esp_lcd_panel_io_handle_t, const esp_lcd_touch_config_t *,
                                       esp_lcd_touch_handle_t *);
esp_err_t esp_lcd_touch_del(esp_lcd_touch_handle_t);
esp_err_t esp_lcd_touch_read_data(esp_lcd_touch_handle_t);
esp_err_t esp_lcd_touch_get_data(esp_lcd_touch_handle_t, esp_lcd_touch_point_data_t *, uint8_t *,
                                 uint8_t);
typedef int BaseType_t;
typedef uint32_t TickType_t;
#define pdTRUE 1
#define pdFALSE 0
#define pdMS_TO_TICKS(ms) (ms)
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(lock) ((void)(lock))
#define portEXIT_CRITICAL(lock) ((void)(lock))
typedef struct host_semaphore {
    bool active, binary;
    unsigned count;
} StaticSemaphore_t;
typedef StaticSemaphore_t *SemaphoreHandle_t;
SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *);
SemaphoreHandle_t xSemaphoreCreateBinaryStatic(StaticSemaphore_t *);
int xSemaphoreTake(SemaphoreHandle_t, TickType_t);
int xSemaphoreGive(SemaphoreHandle_t);
int xSemaphoreGiveFromISR(SemaphoreHandle_t, BaseType_t *);
void vSemaphoreDelete(SemaphoreHandle_t);
typedef struct host_display lv_display_t;
typedef struct host_timer lv_timer_t;
typedef struct host_input lv_indev_t;
typedef struct {
    int x1, y1, x2, y2;
} lv_area_t;
typedef struct {
    int state;
    struct {
        int x, y;
    } point;
} lv_indev_data_t;
typedef void (*lv_display_flush_cb_t)(lv_display_t *, const lv_area_t *, uint8_t *);
typedef void (*lv_timer_cb_t)(lv_timer_t *);
typedef void (*lv_indev_read_cb_t)(lv_indev_t *, lv_indev_data_t *);
#define LV_INDEV_STATE_RELEASED 0
#define LV_INDEV_STATE_PRESSED 1
#define LV_INDEV_TYPE_POINTER 1
#define LV_COLOR_FORMAT_RGB565 1
#define LV_DISPLAY_RENDER_MODE_FULL 1
lv_display_t *lv_display_create(int, int);
void lv_display_delete(lv_display_t *);
void lv_display_delete_refr_timer(lv_display_t *);
void lv_display_set_color_format(lv_display_t *, int);
void lv_display_set_buffers(lv_display_t *, void *, void *, size_t, int);
void lv_display_set_flush_cb(lv_display_t *, lv_display_flush_cb_t);
bool lv_display_flush_is_last(lv_display_t *);
void lv_display_flush_ready(lv_display_t *);
lv_indev_t *lv_indev_create(void);
void lv_indev_delete(lv_indev_t *);
void lv_indev_set_type(lv_indev_t *, int);
void lv_indev_set_display(lv_indev_t *, lv_display_t *);
void lv_indev_set_read_cb(lv_indev_t *, lv_indev_read_cb_t);
lv_timer_t *lv_indev_get_read_timer(lv_indev_t *);
lv_timer_t *lv_timer_create(lv_timer_cb_t, uint32_t, void *);
void lv_timer_delete(lv_timer_t *);
void lv_timer_set_period(lv_timer_t *, uint32_t);
typedef struct {
    int task_priority, task_stack, task_affinity, task_max_sleep_ms;
    unsigned task_stack_caps;
    int timer_period_ms;
} lvgl_port_cfg_t;
#define ESP_LVGL_PORT_INIT_CONFIG()                                                                \
    { 4, 7168, -1, 500, 0, 5 }
#define LVGL_PORT_EVENT_USER 0x80
esp_err_t lvgl_port_init(const lvgl_port_cfg_t *);
esp_err_t lvgl_port_deinit(void);
bool lvgl_port_lock(uint32_t);
void lvgl_port_unlock(void);
esp_err_t lvgl_port_task_wake(int, void *);
#ifdef __cplusplus
}
#endif
#endif
