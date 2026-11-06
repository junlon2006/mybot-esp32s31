/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "freertos/task.h"
#include "usb/usb_host.h"
#include "usb_host_probe.h"

/* Exercise the public API and captured task/callback. Every scenario gets a fresh
 * process, including the production one-attempt guard. No USB hardware is implied. */
static const char *scenario;
static unsigned checks, task_calls, install_calls, register_calls, uninstall_calls;
static unsigned library_calls, client_calls, free_calls, open_calls, close_calls;
static unsigned info_calls, descriptor_calls, config_calls, printed_devices, printed_configs;
static unsigned callback_depth, loop, close_failures;
static bool library_live, client_live, device_live, library_cleanup_done;
static int task_token, client_token, device_tokens[2];
static uint8_t open_address;
static usb_device_handle_t open_handle;
static TaskFunction_t worker;
static void *worker_argument;
static usb_host_client_event_cb_t callback;
static void *callback_argument;
static jmp_buf worker_exit;

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        assert(condition);                                                                         \
        ++checks;                                                                                  \
    } while (0)

static bool is_case(const char *name) {
    return !strcmp(scenario, name);
}

const char *esp_err_to_name(esp_err_t error) {
    return error == ESP_OK ? "ESP_OK" : "injected_error";
}

void test_log(const char *tag, const char *format, ...) {
    CHECK(tag && format && !callback_depth);
    char text[2048];
    va_list arguments;
    va_start(arguments, format);
    int length = vsnprintf(text, sizeof(text), format, arguments);
    va_end(arguments);
    CHECK(length >= 0 && (size_t)length < sizeof(text));
}

BaseType_t xTaskCreatePinnedToCore(TaskFunction_t task, const char *name, uint32_t stack,
                                   void *argument, unsigned priority, TaskHandle_t *handle,
                                   BaseType_t core) {
    CHECK(!worker && task && name && stack >= 3072 && priority > 0 && core == 0);
    ++task_calls;
    if (is_case("task_failure"))
        return pdFALSE;
    worker = task;
    worker_argument = argument;
    if (handle)
        *handle = &task_token;
    return pdPASS;
}

void vTaskDelete(TaskHandle_t task) {
    CHECK(!task && !callback_depth);
    longjmp(worker_exit, 1);
}

void vTaskDelay(TickType_t ticks) {
    CHECK(ticks > 0 && ticks <= 1000 && !callback_depth);
}

static void require_client(usb_host_client_handle_t client) {
    CHECK(library_live && client_live && client == &client_token && !callback_depth);
}

static void require_device(usb_device_handle_t device) {
    CHECK(library_live && client_live && device_live && device == open_handle && !callback_depth);
}

esp_err_t usb_host_install(const usb_host_config_t *config) {
    CHECK(!library_live && config && !config->skip_phy_setup && config->intr_flags);
    CHECK(!config->root_port_unpowered && !config->peripheral_map && !config->fsls_only);
    ++install_calls;
    if (is_case("install_failure"))
        return ESP_ERR_NO_MEM;
    library_live = true;
    return ESP_OK;
}

esp_err_t usb_host_uninstall(void) {
    CHECK(library_live && !client_live && !device_live && !callback_depth);
    CHECK(!is_case("cleanup_pending") || library_cleanup_done);
    ++uninstall_calls;
    if (is_case("uninstall_failure"))
        return ESP_FAIL;
    library_live = false;
    return ESP_OK;
}

esp_err_t usb_host_client_register(const usb_host_client_config_t *config,
                                   usb_host_client_handle_t *client) {
    CHECK(library_live && !client_live && config && !config->is_synchronous);
    CHECK(config->max_num_event_msg > 0 && config->async.client_event_callback && client);
    ++register_calls;
    if (is_case("register_failure") || is_case("cleanup_pending") || is_case("cleanup_timeout") ||
        is_case("cleanup_error") || is_case("free_failure") || is_case("uninstall_failure"))
        return ESP_ERR_NO_MEM;
    client_live = true;
    callback = config->async.client_event_callback;
    callback_argument = config->async.callback_arg;
    *client = &client_token;
    return ESP_OK;
}

esp_err_t usb_host_device_free_all(void) {
    CHECK(library_live && !client_live && !device_live && !callback_depth);
    ++free_calls;
    if (is_case("free_failure"))
        return ESP_FAIL;
    if (is_case("cleanup_timeout") || is_case("cleanup_error"))
        return ESP_ERR_NOT_FINISHED;
    if (is_case("cleanup_pending") && !library_cleanup_done)
        return ESP_ERR_NOT_FINISHED;
    library_cleanup_done = true;
    return ESP_OK;
}

esp_err_t usb_host_lib_handle_events(TickType_t timeout, uint32_t *flags) {
    CHECK(library_live && flags && timeout <= 1000 && !callback_depth);
    ++library_calls;
    CHECK(library_calls < 100); /* Failed cleanup must terminate, not spin forever. */
    *flags = 0;
    if (!client_live && is_case("cleanup_pending") && library_calls == 2) {
        library_cleanup_done = true;
        *flags = USB_HOST_LIB_EVENT_FLAGS_ALL_FREE;
    }
    if (is_case("library_error") && library_calls == 1)
        return ESP_FAIL;
    if (is_case("cleanup_error"))
        return ESP_FAIL;
    return ESP_OK;
}

static void new_device(uint8_t address) {
    usb_host_client_event_msg_t event = {
        .event = USB_HOST_CLIENT_EVENT_NEW_DEV,
        .new_dev = {.address = address},
    };
    ++callback_depth;
    callback(&event, callback_argument);
    --callback_depth;
}

static void device_gone(void) {
    if (!device_live)
        return;
    usb_host_client_event_msg_t event = {
        .event = USB_HOST_CLIENT_EVENT_DEV_GONE,
        .dev_gone = {.dev_hdl = open_handle},
    };
    ++callback_depth;
    callback(&event, callback_argument);
    --callback_depth;
}

esp_err_t usb_host_client_handle_events(usb_host_client_handle_t client, TickType_t timeout) {
    require_client(client);
    CHECK(timeout > 0 && timeout <= 1000);
    ++client_calls;
    ++loop;
    if (loop == 8)
        longjmp(worker_exit, 2);
    if (is_case("client_error") && loop == 1)
        return ESP_FAIL;
    if (is_case("idle"))
        return ESP_ERR_TIMEOUT;
    if (loop == 1 || (is_case("client_error") && loop == 2))
        new_device(1);
    if (loop == 3) {
        device_gone();
        if (is_case("queued_replug") || is_case("close_retry") || is_case("close_failure"))
            new_device(2);
    }
    if (loop == 5 && !is_case("queued_replug") && !is_case("close_retry") &&
        !is_case("close_failure"))
        new_device(2);
    if (loop == 7)
        device_gone();
    return ESP_OK;
}

esp_err_t usb_host_device_open(usb_host_client_handle_t client, uint8_t address,
                               usb_device_handle_t *device) {
    require_client(client);
    CHECK(!device_live && device && (address == 1 || address == 2));
    ++open_calls;
    if (is_case("open_failure") && open_calls == 1)
        return ESP_FAIL;
    device_live = true;
    open_address = address;
    open_handle = &device_tokens[address - 1];
    *device = open_handle;
    return ESP_OK;
}

esp_err_t usb_host_device_close(usb_host_client_handle_t client, usb_device_handle_t device) {
    require_client(client);
    require_device(device);
    ++close_calls;
    if (is_case("close_failure") || (is_case("close_retry") && close_failures++ == 0))
        return ESP_FAIL;
    device_live = false;
    open_handle = NULL;
    return ESP_OK;
}

esp_err_t usb_host_device_info(usb_device_handle_t device, usb_device_info_t *info) {
    require_device(device);
    CHECK(info);
    ++info_calls;
    if (is_case("info_failure") && info_calls == 1)
        return ESP_FAIL;
    *info = (usb_device_info_t){
        .speed = USB_SPEED_HIGH,
        .dev_addr = open_address,
        .bMaxPacketSize0 = 64,
        .bConfigurationValue = 1,
    };
    return ESP_OK;
}

static const usb_device_desc_t device_descriptor = {
    .bLength = 18,
    .bDescriptorType = 1,
    .bcdUSB = 0x0200,
    .bMaxPacketSize0 = 64,
    .idVendor = 0x1234,
    .idProduct = 0x5678,
    .bNumConfigurations = 1,
};
static const usb_config_desc_t config_descriptor = {
    .bLength = 9,
    .bDescriptorType = 2,
    .wTotalLength = 9,
    .bNumInterfaces = 0,
    .bConfigurationValue = 1,
    .bmAttributes = 0x80,
    .bMaxPower = 250,
};

esp_err_t usb_host_get_device_descriptor(usb_device_handle_t device,
                                         const usb_device_desc_t **descriptor) {
    require_device(device);
    CHECK(descriptor);
    ++descriptor_calls;
    if (is_case("device_descriptor_failure") && descriptor_calls == 1)
        return ESP_FAIL;
    *descriptor = &device_descriptor;
    return ESP_OK;
}

esp_err_t usb_host_get_active_config_descriptor(usb_device_handle_t device,
                                                const usb_config_desc_t **descriptor) {
    require_device(device);
    CHECK(descriptor);
    ++config_calls;
    if (is_case("config_descriptor_failure") && config_calls == 1)
        return ESP_FAIL;
    *descriptor = &config_descriptor;
    return ESP_OK;
}

void usb_print_device_descriptor(const usb_device_desc_t *descriptor) {
    CHECK(!callback_depth && device_live && descriptor == &device_descriptor);
    ++printed_devices;
}

void usb_print_config_descriptor(const usb_config_desc_t *descriptor,
                                 print_class_descriptor_cb callback_function) {
    CHECK(!callback_depth && device_live && descriptor == &config_descriptor && !callback_function);
    ++printed_configs;
}

int main(int argc, char **argv) {
    CHECK(argc == 2);
    scenario = argv[1];
    esp_err_t result = mybot_usb_host_probe_start();
    CHECK(result == (is_case("task_failure") ? ESP_ERR_NO_MEM : ESP_OK));
    CHECK(mybot_usb_host_probe_start() == ESP_ERR_INVALID_STATE);
    CHECK(task_calls == 1);
    if (is_case("task_failure")) {
        CHECK(!worker && !install_calls);
    } else {
        int reason = setjmp(worker_exit);
        if (!reason) {
            worker(worker_argument);
            CHECK(!"worker must terminate via task deletion or the bounded mock event loop");
        }
        CHECK(install_calls == 1);
        CHECK(mybot_usb_host_probe_start() == ESP_ERR_INVALID_STATE);
        CHECK(task_calls == 1);
        if (is_case("install_failure")) {
            CHECK(reason == 1 && !register_calls && !uninstall_calls && !library_live);
        } else if (!client_live) {
            CHECK(reason == 1 && register_calls == 1 && free_calls && !open_calls);
            if (is_case("cleanup_timeout") || is_case("cleanup_error") || is_case("free_failure")) {
                CHECK(!uninstall_calls && library_live);
                if (!is_case("free_failure"))
                    CHECK(library_calls > 0);
            } else {
                CHECK(uninstall_calls == 1);
                CHECK(library_live == is_case("uninstall_failure"));
            }
        } else {
            CHECK(reason == 2 && !free_calls && !uninstall_calls);
            CHECK(device_live == is_case("close_failure"));
            if (is_case("idle")) {
                CHECK(!open_calls && !close_calls);
            } else if (is_case("close_failure")) {
                CHECK(open_calls == 1 && close_calls == 3);
            } else {
                CHECK(open_calls == 2);
                CHECK(close_calls == (is_case("open_failure")  ? 1U
                                      : is_case("close_retry") ? 3U
                                                               : 2U));
                CHECK(printed_configs >= 1 && printed_configs <= 2);
                if (is_case("normal") || is_case("queued_replug") || is_case("close_retry"))
                    CHECK(printed_devices == 2 && printed_configs == 2);
            }
        }
    }
    printf("PASS usb_host_probe %s (%u checks)\n", scenario, checks);
    return 0;
}
