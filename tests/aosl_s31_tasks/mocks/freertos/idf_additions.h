/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_TEST_AOSL_S31_TASKS_MOCKS_FREERTOS_IDF_ADDITIONS_H_
#define MYBOT_TEST_AOSL_S31_TASKS_MOCKS_FREERTOS_IDF_ADDITIONS_H_

#include "task.h"
BaseType_t xTaskCreateWithCaps(TaskFunction_t, const char *, configSTACK_DEPTH_TYPE, void *,
                               UBaseType_t, TaskHandle_t *, UBaseType_t);
void vTaskDeleteWithCaps(TaskHandle_t);
StackType_t *xTaskGetStackStart(TaskHandle_t);

#endif /* MYBOT_TEST_AOSL_S31_TASKS_MOCKS_FREERTOS_IDF_ADDITIONS_H_ */
