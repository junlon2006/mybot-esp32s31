/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_TEST_AOSL_S31_TASKS_MOCKS_ESP_HEAP_CAPS_H_
#define MYBOT_TEST_AOSL_S31_TASKS_MOCKS_ESP_HEAP_CAPS_H_

#include <stddef.h>
#include <stdint.h>
#define MALLOC_CAP_8BIT 1U
#define MALLOC_CAP_INTERNAL 2U
#define MALLOC_CAP_SPIRAM 4U
size_t heap_caps_get_free_size(uint32_t);
size_t heap_caps_get_largest_free_block(uint32_t);

#endif /* MYBOT_TEST_AOSL_S31_TASKS_MOCKS_ESP_HEAP_CAPS_H_ */
