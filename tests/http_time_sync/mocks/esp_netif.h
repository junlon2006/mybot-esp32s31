/* SPDX-License-Identifier: Apache-2.0 */
#ifndef TEST_ESP_NETIF_H
#define TEST_ESP_NETIF_H

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL (-1)
typedef esp_err_t (*esp_netif_callback_fn)(void *context);
esp_err_t esp_netif_tcpip_exec(esp_netif_callback_fn callback, void *context);

#endif
