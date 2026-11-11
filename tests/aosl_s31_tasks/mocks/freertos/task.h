/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_TEST_AOSL_S31_TASKS_MOCKS_FREERTOS_TASK_H_
#define MYBOT_TEST_AOSL_S31_TASKS_MOCKS_FREERTOS_TASK_H_

#include "FreeRTOS.h"
typedef void (*TaskFunction_t)(void *);
typedef struct mock_task *TaskHandle_t;
BaseType_t xTaskCreate(TaskFunction_t, const char *, configSTACK_DEPTH_TYPE, void *, UBaseType_t,
                       TaskHandle_t *);
TaskHandle_t xTaskCreateStatic(TaskFunction_t, const char *, configSTACK_DEPTH_TYPE, void *,
                               UBaseType_t, StackType_t *, StaticTask_t *);
void vTaskDelete(TaskHandle_t);
void vTaskSuspend(TaskHandle_t);
void vTaskDelay(TickType_t);
TaskHandle_t xTaskGetCurrentTaskHandle(void);
const char *pcTaskGetName(TaskHandle_t);

#endif /* MYBOT_TEST_AOSL_S31_TASKS_MOCKS_FREERTOS_TASK_H_ */
