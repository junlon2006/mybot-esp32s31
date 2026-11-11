/* SPDX-License-Identifier: Apache-2.0 */
#ifndef MYBOT_TEST_BOOTSTRAP_MAIN_MOCKS_ESP_APP_DESC_H_
#define MYBOT_TEST_BOOTSTRAP_MAIN_MOCKS_ESP_APP_DESC_H_

typedef struct {
    const char *date;
    const char *time;
    const char *version;
} esp_app_desc_t;

const esp_app_desc_t *esp_app_get_description(void);

#endif /* MYBOT_TEST_BOOTSTRAP_MAIN_MOCKS_ESP_APP_DESC_H_ */
