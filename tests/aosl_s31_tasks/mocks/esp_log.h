/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
void mock_log(const char *tag, const char *format, ...) __attribute__((format(printf, 2, 3)));
#define ESP_LOGI mock_log
#define ESP_LOGE mock_log
