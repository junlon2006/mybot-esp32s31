/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

typedef struct {
    const char *date;
    const char *time;
    const char *version;
} esp_app_desc_t;

const esp_app_desc_t *esp_app_get_description(void);
