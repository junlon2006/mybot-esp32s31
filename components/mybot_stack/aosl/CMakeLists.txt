# SPDX-License-Identifier: Apache-2.0
# The upstream AOSL sources remain unchanged. This wrapper supplies the S31
# configuration and resolves all AOSL symbols before the bundled RTSA archive.
set(AOSL_DIR "${COMPONENT_PATH}")
set(CONFIG_PLATFORM "esp32-s3")
set(AOSL_DECLARE_PROJECT OFF)
set(AOSL_ADD_OPTIONS)
set(AOSL_ADD_DEFINITIONS)
set(AOSL_ADD_INCLUDES_PUBLIC)
set(AOSL_ADD_INCLUDES_PRIVATE)
set(AOSL_ADD_SOURCES)
include("${AOSL_DIR}/cmake/build.cmake")

list(REMOVE_ITEM AOSL_ADD_SOURCES "${AOSL_DIR}/test/aosl_test.c")
list(REMOVE_ITEM AOSL_ADD_INCLUDES_PUBLIC
     "${AOSL_DIR}/platform/src/esp32-s3/config")
list(PREPEND AOSL_ADD_INCLUDES_PUBLIC "${COMPONENT_PATH}/s31_port/config")
list(APPEND AOSL_ADD_SOURCES
     "${COMPONENT_PATH}/s31_port/aosl_hal_os_version.c")

idf_component_register(SRCS ${AOSL_ADD_SOURCES}
                       INCLUDE_DIRS ${AOSL_ADD_INCLUDES_PUBLIC}
                       PRIV_INCLUDE_DIRS ${AOSL_ADD_INCLUDES_PRIVATE}
                       REQUIRES spi_flash lwip mbedtls freertos esp_netif
                       PRIV_REQUIRES esp_hw_support pthread heap
                       WHOLE_ARCHIVE)
set_source_files_properties("${AOSL_DIR}/platform/src/esp32-s3/aosl_hal_utils.c"
    PROPERTIES COMPILE_DEFINITIONS
    "aosl_hal_os_version=mybot_aosl_legacy_os_version")
target_compile_options(${COMPONENT_LIB} PRIVATE ${AOSL_ADD_OPTIONS})
target_compile_definitions(${COMPONENT_LIB} PRIVATE ${AOSL_ADD_DEFINITIONS})
