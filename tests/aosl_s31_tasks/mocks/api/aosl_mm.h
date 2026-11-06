/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stddef.h>
void *aosl_malloc(size_t);
void *aosl_calloc(size_t, size_t);
void aosl_free(void *);
