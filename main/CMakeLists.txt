if(CONFIG_LWIP_MAX_SOCKETS LESS 24)
    message(FATAL_ERROR
        "mybot requires CONFIG_LWIP_MAX_SOCKETS >= 24 (recommended: 32); "
        "remove the stale sdkconfig and reconfigure")
endif()

if(CONFIG_LWIP_SNTP_MAX_SERVERS LESS 3)
    message(FATAL_ERROR
        "mybot requires CONFIG_LWIP_SNTP_MAX_SERVERS >= 3; "
        "regenerate the stale sdkconfig from project defaults")
endif()

set(mybot_main_sources "app_main.c")
if(CONFIG_MYBOT_DEBUG_RESOURCE_MONITOR)
    list(APPEND mybot_main_sources "resource_monitor.c")
endif()
if(CONFIG_MYBOT_USB_CDC_PROBE)
    list(APPEND mybot_main_sources "usb_cdc_probe.c" "usb_cdc_probe_diag.c")
endif()
if(CONFIG_MYBOT_USB_HOST_PROBE)
    list(APPEND mybot_main_sources "usb_host_probe.c")
endif()

idf_component_register(
    SRCS ${mybot_main_sources}
    REQUIRES
        mybot_sdk
        mybot_platform
        agora_rtc
        aosl
        nvs_flash
        esp_psram
        esp_app_format
        esp_netif
        esp_event
        esp_timer
        heap
        mbedtls
    PRIV_REQUIRES
        esp_hal_usb
)
