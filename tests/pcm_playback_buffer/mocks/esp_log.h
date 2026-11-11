/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_TEST_PCM_PLAYBACK_BUFFER_MOCKS_ESP_LOG_H_
#define MYBOT_TEST_PCM_PLAYBACK_BUFFER_MOCKS_ESP_LOG_H_

void host_log(const char *format, ...) __attribute__((format(printf, 1, 2)));
#define ESP_LOGI(tag, ...) host_log(__VA_ARGS__)
#define ESP_LOGW(tag, ...) host_log(__VA_ARGS__)
#define ESP_LOGE(tag, ...) host_log(__VA_ARGS__)

#endif /* MYBOT_TEST_PCM_PLAYBACK_BUFFER_MOCKS_ESP_LOG_H_ */
