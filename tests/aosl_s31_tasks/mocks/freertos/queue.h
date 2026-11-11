/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_TEST_AOSL_S31_TASKS_MOCKS_FREERTOS_QUEUE_H_
#define MYBOT_TEST_AOSL_S31_TASKS_MOCKS_FREERTOS_QUEUE_H_

#include "FreeRTOS.h"
typedef struct mock_queue {
    UBaseType_t capacity, item_size, head, count;
    uint8_t *items;
} StaticQueue_t;
typedef StaticQueue_t *QueueHandle_t;
QueueHandle_t xQueueCreateStatic(UBaseType_t, UBaseType_t, uint8_t *, StaticQueue_t *);
void vQueueDelete(QueueHandle_t);
BaseType_t xQueueSend(QueueHandle_t, const void *, TickType_t);
BaseType_t xQueueReceive(QueueHandle_t, void *, TickType_t);

#endif /* MYBOT_TEST_AOSL_S31_TASKS_MOCKS_FREERTOS_QUEUE_H_ */
