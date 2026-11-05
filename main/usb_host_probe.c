/* SPDX-License-Identifier: Apache-2.0 */
#include "usb_host_probe.h"

#include "esp_intr_alloc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "usb/usb_host.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PROBE_WORKER_STACK 4096U
#define PROBE_CLOSE_ATTEMPTS 3U
#define PROBE_CLEANUP_STEPS 20U

static const char *TAG = "usb_host_probe";

typedef struct {
    usb_host_client_handle_t client;
    usb_device_handle_t device;
    uint8_t address;
    uint8_t pending_address;
    unsigned close_attempts;
    bool disconnected;
    bool close_pending;
} probe_state_t;

/* All USB state and callbacks belong to one task. Static storage also remains
 * valid if an unrecoverable driver cleanup failure requires a reboot. */
static probe_state_t s_probe;
static bool s_start_attempted;

static TickType_t poll_ticks(void) {
    TickType_t ticks = pdMS_TO_TICKS(10);
    return ticks ? ticks : 1;
}

static void report_error(const char *operation, esp_err_t error) {
    ESP_LOGE(TAG, "event=usb_host_error operation=%s error=%s", operation, esp_err_to_name(error));
}

/* The library invokes this inside client_handle_events(), on our worker.
 * Defer device API calls and logging until the handler has returned. */
static void client_event(const usb_host_client_event_msg_t *event, void *arg) {
    probe_state_t *probe = arg;
    switch (event->event) {
    case USB_HOST_CLIENT_EVENT_NEW_DEV:
        probe->pending_address = event->new_dev.address;
        break;
    case USB_HOST_CLIENT_EVENT_DEV_GONE:
        if (probe->device == event->dev_gone.dev_hdl) {
            probe->disconnected = true;
            probe->close_pending = true;
        }
        break;
    default:
        break;
    }
}

static const char *speed_name(usb_speed_t speed) {
    switch (speed) {
    case USB_SPEED_LOW:
        return "low";
    case USB_SPEED_FULL:
        return "full";
    case USB_SPEED_HIGH:
        return "high";
    default:
        return "unknown";
    }
}

static void describe_device(probe_state_t *probe) {
    usb_device_info_t info;
    esp_err_t result = usb_host_device_info(probe->device, &info);
    if (result != ESP_OK) {
        report_error("device_info", result);
        return;
    }
    const usb_device_desc_t *descriptor;
    result = usb_host_get_device_descriptor(probe->device, &descriptor);
    if (result != ESP_OK) {
        report_error("device_descriptor", result);
        return;
    }
    ESP_LOGI(TAG, "event=usb_host_device address=%u vid=%04x pid=%04x speed=%s", probe->address,
             descriptor->idVendor, descriptor->idProduct, speed_name(info.speed));
    usb_print_device_descriptor(descriptor);

    const usb_config_desc_t *configuration;
    result = usb_host_get_active_config_descriptor(probe->device, &configuration);
    if (result != ESP_OK) {
        report_error("config_descriptor", result);
        return;
    }
    /* Cached descriptors only: no interface claims or class transfers. Do not
     * print string descriptors, which can contain the phone's serial number. */
    usb_print_config_descriptor(configuration, NULL);
    ESP_LOGI(TAG, "event=usb_host_descriptors_complete address=%u", probe->address);
}

static void handle_device(probe_state_t *probe) {
    if (probe->disconnected) {
        ESP_LOGI(TAG, "event=usb_host_disconnected address=%u", probe->address);
        probe->disconnected = false;
    }
    if (probe->close_pending && probe->close_attempts < PROBE_CLOSE_ATTEMPTS) {
        esp_err_t result = usb_host_device_close(probe->client, probe->device);
        if (result != ESP_OK) {
            report_error("device_close", result);
            if (++probe->close_attempts == PROBE_CLOSE_ATTEMPTS)
                ESP_LOGE(TAG, "event=usb_host_cleanup_failed resources=retained_until_reboot");
            return;
        }
        probe->device = NULL;
        probe->address = 0;
        probe->close_pending = false;
        probe->close_attempts = 0;
    }
    if (probe->device || !probe->pending_address)
        return;

    uint8_t address = probe->pending_address;
    probe->pending_address = 0;
    ESP_LOGI(TAG, "event=usb_host_new_device address=%u", address);
    esp_err_t result = usb_host_device_open(probe->client, address, &probe->device);
    if (result != ESP_OK) {
        probe->device = NULL;
        report_error("device_open", result);
        return;
    }
    probe->address = address;
    describe_device(probe);
}

/* Used only when client registration failed. No client owns a device and no
 * application callback was registered. Keep cleanup bounded; a failed teardown
 * leaves the library's resources alive until reboot. */
static void cleanup_unregistered_host(void) {
    esp_err_t result = usb_host_device_free_all();
    if (result == ESP_ERR_NOT_FINISHED) {
        for (unsigned i = 0; i < PROBE_CLEANUP_STEPS; ++i) {
            uint32_t flags = 0;
            esp_err_t handled = usb_host_lib_handle_events(poll_ticks(), &flags);
            if (handled != ESP_OK && handled != ESP_ERR_TIMEOUT) {
                result = handled;
                break;
            }
            if (flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
                result = ESP_OK;
                break;
            }
        }
    }
    if (result == ESP_OK)
        result = usb_host_uninstall();
    if (result != ESP_OK) {
        report_error("startup_cleanup", result);
        ESP_LOGE(TAG, "event=usb_host_cleanup_failed resources=retained_until_reboot");
    }
}

static void probe_worker(void *arg) {
    probe_state_t *probe = arg;
    const usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LOWMED,
    };
    esp_err_t result = usb_host_install(&host_config);
    if (result != ESP_OK) {
        report_error("install", result);
        vTaskDelete(NULL);
        return;
    }
    const usb_host_client_config_t client_config = {
        .is_synchronous = false,
        .max_num_event_msg = 8,
        .async =
            {
                .client_event_callback = client_event,
                .callback_arg = probe,
            },
    };
    result = usb_host_client_register(&client_config, &probe->client);
    if (result != ESP_OK) {
        report_error("client_register", result);
        cleanup_unregistered_host();
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "event=usb_host_ready mode=host port=default action=waiting_for_device");

    esp_err_t last_library_error = ESP_OK;
    esp_err_t last_client_error = ESP_OK;
    for (;;) {
        uint32_t flags = 0;
        result = usb_host_lib_handle_events(0, &flags);
        if (result != ESP_OK && result != ESP_ERR_TIMEOUT) {
            if (result != last_library_error)
                report_error("library_events", result);
            last_library_error = result;
        } else {
            last_library_error = ESP_OK;
        }
        result = usb_host_client_handle_events(probe->client, poll_ticks());
        if (result != ESP_OK && result != ESP_ERR_TIMEOUT) {
            if (result != last_client_error)
                report_error("client_events", result);
            last_client_error = result;
            vTaskDelay(poll_ticks());
        } else {
            last_client_error = ESP_OK;
        }
        handle_device(probe);
    }
}

esp_err_t mybot_usb_host_probe_start(void) {
    if (s_start_attempted)
        return ESP_ERR_INVALID_STATE;
    s_start_attempted = true;
    if (xTaskCreatePinnedToCore(probe_worker, "usb_host_probe", PROBE_WORKER_STACK, &s_probe, 4,
                                NULL, 0) != pdPASS)
        return ESP_ERR_NO_MEM;
    return ESP_OK;
}
