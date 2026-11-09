/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_HOST_AOSL_HAL_MEMORY_H
#define MYBOT_HOST_AOSL_HAL_MEMORY_H

#include <stddef.h>

void *aosl_hal_malloc(size_t size);
void aosl_hal_free(void *memory);

#endif
