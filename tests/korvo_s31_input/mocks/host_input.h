/* SPDX-License-Identifier: MIT */
#ifndef KORVO_S31_HOST_INPUT_H_
#define KORVO_S31_HOST_INPUT_H_
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define pdPASS 1
#define pdTRUE 1
#define pdFALSE 0
#define portMAX_DELAY UINT32_MAX
#define pdMS_TO_TICKS(ms) (ms)
#define MALLOC_CAP_SPIRAM 1U
#define MALLOC_CAP_8BIT 2U
typedef uint32_t TickType_t;
typedef pthread_mutex_t StaticSemaphore_t;
typedef StaticSemaphore_t *SemaphoreHandle_t;
SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *);
int xSemaphoreTake(SemaphoreHandle_t, TickType_t);
int xSemaphoreGive(SemaphoreHandle_t);
typedef struct host_adc *adc_oneshot_unit_handle_t;
typedef int adc_unit_t;
typedef int adc_channel_t;
#define ADC_UNIT_1 0
#define ADC_CHANNEL_0 0
#define ADC_ATTEN_DB_0 0
#define ADC_BITWIDTH_DEFAULT 0
typedef struct {
    int unit_id;
} adc_oneshot_unit_init_cfg_t;
typedef struct {
    int atten, bitwidth;
} adc_oneshot_chan_cfg_t;
esp_err_t adc_oneshot_new_unit(const adc_oneshot_unit_init_cfg_t *, adc_oneshot_unit_handle_t *);
esp_err_t adc_oneshot_config_channel(adc_oneshot_unit_handle_t, adc_channel_t,
                                     const adc_oneshot_chan_cfg_t *);
esp_err_t adc_oneshot_del_unit(adc_oneshot_unit_handle_t);
esp_err_t adc_oneshot_read(adc_oneshot_unit_handle_t, adc_channel_t, int *);
esp_err_t bsp_s31_adc_calibration_init(adc_oneshot_unit_handle_t, adc_unit_t, adc_channel_t);
esp_err_t bsp_s31_adc_calibration_raw_to_mv(adc_unit_t, int, int *);
typedef struct host_task *TaskHandle_t;
typedef void (*task_function_t)(void *);
int xTaskCreateWithCaps(task_function_t, const char *, uint32_t, void *, unsigned, TaskHandle_t *,
                        unsigned);
void vTaskDelay(TickType_t);
int64_t esp_timer_get_time(void);
typedef void *i2c_master_bus_handle_t;
#ifdef __cplusplus
}
#endif
#endif
