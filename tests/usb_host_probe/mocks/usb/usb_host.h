/* SPDX-License-Identifier: Apache-2.0 */
#ifndef TEST_USB_HOST_PROBE_USB_HOST_H
#define TEST_USB_HOST_PROBE_USB_HOST_H
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

typedef void *usb_device_handle_t;
typedef void *usb_host_client_handle_t;
typedef enum { USB_SPEED_LOW, USB_SPEED_FULL, USB_SPEED_HIGH } usb_speed_t;
typedef struct __attribute__((packed)) {
    uint8_t bLength, bDescriptorType;
    uint16_t bcdUSB;
    uint8_t bDeviceClass, bDeviceSubClass, bDeviceProtocol, bMaxPacketSize0;
    uint16_t idVendor, idProduct, bcdDevice;
    uint8_t iManufacturer, iProduct, iSerialNumber, bNumConfigurations;
} usb_device_desc_t;
typedef struct __attribute__((packed)) {
    uint8_t bLength, bDescriptorType;
    uint16_t wTotalLength;
    uint8_t bNumInterfaces, bConfigurationValue, iConfiguration, bmAttributes, bMaxPower;
} usb_config_desc_t;
typedef struct {
    usb_speed_t speed;
    uint8_t dev_addr, bMaxPacketSize0, bConfigurationValue;
    const void *str_desc_manufacturer, *str_desc_product, *str_desc_serial_num;
} usb_device_info_t;
typedef struct {
    bool skip_phy_setup;
    bool root_port_unpowered;
    int intr_flags;
    unsigned peripheral_map;
    bool fsls_only;
} usb_host_config_t;
typedef enum {
    USB_HOST_CLIENT_EVENT_NEW_DEV,
    USB_HOST_CLIENT_EVENT_DEV_GONE,
} usb_host_client_event_t;
typedef struct {
    usb_host_client_event_t event;
    union {
        struct {
            uint8_t address;
        } new_dev;
        struct {
            usb_device_handle_t dev_hdl;
        } dev_gone;
    };
} usb_host_client_event_msg_t;
typedef void (*usb_host_client_event_cb_t)(const usb_host_client_event_msg_t *event, void *arg);
typedef struct {
    bool is_synchronous;
    int max_num_event_msg;
    struct {
        usb_host_client_event_cb_t client_event_callback;
        void *callback_arg;
    } async;
} usb_host_client_config_t;
typedef struct {
    uint8_t bLength, bDescriptorType;
} usb_standard_desc_t;
typedef void (*print_class_descriptor_cb)(const usb_standard_desc_t *descriptor);

#define USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS (1U << 0)
#define USB_HOST_LIB_EVENT_FLAGS_ALL_FREE (1U << 1)
esp_err_t usb_host_install(const usb_host_config_t *config);
esp_err_t usb_host_uninstall(void);
esp_err_t usb_host_client_register(const usb_host_client_config_t *config,
                                   usb_host_client_handle_t *client);
esp_err_t usb_host_lib_handle_events(TickType_t timeout, uint32_t *flags);
esp_err_t usb_host_client_handle_events(usb_host_client_handle_t client, TickType_t timeout);
esp_err_t usb_host_device_free_all(void);
esp_err_t usb_host_device_open(usb_host_client_handle_t client, uint8_t address,
                               usb_device_handle_t *device);
esp_err_t usb_host_device_close(usb_host_client_handle_t client, usb_device_handle_t device);
esp_err_t usb_host_device_info(usb_device_handle_t device, usb_device_info_t *info);
esp_err_t usb_host_get_device_descriptor(usb_device_handle_t device,
                                         const usb_device_desc_t **descriptor);
esp_err_t usb_host_get_active_config_descriptor(usb_device_handle_t device,
                                                const usb_config_desc_t **descriptor);
void usb_print_device_descriptor(const usb_device_desc_t *descriptor);
void usb_print_config_descriptor(const usb_config_desc_t *descriptor,
                                 print_class_descriptor_cb callback);
#endif
