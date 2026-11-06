/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_AOSL_TASK_STACK_H
#define MYBOT_AOSL_TASK_STACK_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

BaseType_t mybot_aosl_task_create(TaskFunction_t entry, const char *name,
                                  configSTACK_DEPTH_TYPE stack_bytes, void *argument,
                                  UBaseType_t priority, TaskHandle_t *task);
void mybot_aosl_task_delete(TaskHandle_t task);

#endif
