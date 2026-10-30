/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "esp_err.h"
typedef int gpio_num_t;
#define GPIO_NUM_NC -1
#define GPIO_NUM_2 2
#define GPIO_NUM_3 3
#define GPIO_NUM_4 4
#define GPIO_NUM_5 5
#define GPIO_NUM_6 6
#define GPIO_NUM_7 7
esp_err_t gpio_set_level(gpio_num_t pin, unsigned level);
