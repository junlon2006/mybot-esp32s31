/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_TEST_PCM_PLAYBACK_BUFFER_MOCKS_FREERTOS_IDF_ADDITIONS_H_
#define MYBOT_TEST_PCM_PLAYBACK_BUFFER_MOCKS_FREERTOS_IDF_ADDITIONS_H_

#include "task.h"
BaseType_t xTaskCreateWithCaps(TaskFunction_t function, const char *name, unsigned int stack_bytes,
                               void *argument, unsigned int priority, TaskHandle_t *task,
                               unsigned int caps);
void vTaskDeleteWithCaps(TaskHandle_t task);

#endif /* MYBOT_TEST_PCM_PLAYBACK_BUFFER_MOCKS_FREERTOS_IDF_ADDITIONS_H_ */
