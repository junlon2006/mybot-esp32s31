/* SPDX-License-Identifier: Apache-2.0 */
#ifndef TEST_CAPTIONS_AOSL_MEMORY_H_
#define TEST_CAPTIONS_AOSL_MEMORY_H_
#include <stddef.h>
void *aosl_hal_malloc(size_t size);
void aosl_hal_free(void *memory);
#endif
