/* SPDX-License-Identifier: Apache-2.0 */
#ifndef TEST_USB_HOST_PROBE_TASK_H
#define TEST_USB_HOST_PROBE_TASK_H
#include "FreeRTOS.h"
typedef void *TaskHandle_t;
typedef void (*TaskFunction_t)(void *);
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t task, const char *name, uint32_t stack,
                                   void *argument, unsigned priority, TaskHandle_t *handle,
                                   BaseType_t core);
void vTaskDelete(TaskHandle_t task);
void vTaskDelay(TickType_t ticks);
#endif
