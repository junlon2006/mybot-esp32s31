/* SPDX-License-Identifier: Apache-2.0 */
#ifndef TEST_RTC_MEMORY_H
#define TEST_RTC_MEMORY_H
#include <stddef.h>
void *aosl_hal_malloc(size_t size);
void aosl_hal_free(void *memory);
#endif
