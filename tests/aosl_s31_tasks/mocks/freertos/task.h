/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "FreeRTOS.h"
typedef void (*TaskFunction_t)(void *);
typedef struct mock_task *TaskHandle_t;
BaseType_t xTaskCreate(TaskFunction_t, const char *, configSTACK_DEPTH_TYPE, void *, UBaseType_t,
                       TaskHandle_t *);
void vTaskDelete(TaskHandle_t);
TaskHandle_t xTaskGetCurrentTaskHandle(void);
const char *pcTaskGetName(TaskHandle_t);
