/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "task.h"
BaseType_t xTaskCreateWithCaps(TaskFunction_t, const char *, configSTACK_DEPTH_TYPE, void *,
                               UBaseType_t, TaskHandle_t *, UBaseType_t);
void vTaskDeleteWithCaps(TaskHandle_t);
StackType_t *xTaskGetStackStart(TaskHandle_t);
