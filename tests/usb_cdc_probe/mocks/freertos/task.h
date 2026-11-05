/* SPDX-License-Identifier: Apache-2.0 */
#ifndef TEST_USB_PROBE_TASK_H
#define TEST_USB_PROBE_TASK_H
#include "FreeRTOS.h"
typedef void *TaskHandle_t;
typedef void (*TaskFunction_t)(void *);
BaseType_t xTaskCreate(TaskFunction_t task, const char *name, uint32_t stack, void *argument,
                       unsigned priority, TaskHandle_t *handle);
void xTaskNotifyGive(TaskHandle_t task);
void vTaskDelay(TickType_t ticks);
uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t timeout);
#endif
