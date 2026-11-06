cmake_minimum_required(VERSION 3.16)

# Keep generated configuration beside the selected build, including reconfigure.
if(NOT DEFINED SDKCONFIG AND NOT DEFINED ENV{SDKCONFIG})
    set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig" CACHE FILEPATH "ESP-IDF generated configuration")
endif()

option(MYBOT_MOSAICO_UAC_TEST "Build only the Mosaico V1.2 USB audio diagnostic" OFF)
if(MYBOT_MOSAICO_UAC_TEST)
    set(IDF_TARGET "esp32s31" CACHE STRING "ESP-IDF target")
    set(PROJECT_VER "0.1.0-uac-test")
    set(COMPONENT_DIRS
        "$ENV{IDF_PATH}/components"
        "${CMAKE_CURRENT_LIST_DIR}/components/esp_codec_dev"
        "${CMAKE_CURRENT_LIST_DIR}/diagnostics/mosaico_uac")
    set(COMPONENTS mosaico_uac)
    set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/diagnostics/mosaico_uac/sdkconfig.defaults")
    include($ENV{IDF_PATH}/tools/cmake/project.cmake)
    idf_build_set_property(DEPENDENCIES_LOCK
        "${CMAKE_CURRENT_LIST_DIR}/diagnostics/mosaico_uac/dependencies.lock")
    project(mosaico_uac_test)
    return()
endif()

set(MYBOT_BOARD "esp-mosaico-v1.2" CACHE STRING "mybot target board")
include("${CMAKE_CURRENT_LIST_DIR}/components/mybot_platform/boards/boards.cmake")
set_property(CACHE MYBOT_BOARD PROPERTY STRINGS ${MYBOT_SUPPORTED_BOARDS})
mybot_resolve_board("${MYBOT_BOARD}")
include("${MYBOT_BOARD_PROFILE}")

if(DEFINED MYBOT_CONFIGURED_BOARD AND NOT MYBOT_CONFIGURED_BOARD STREQUAL MYBOT_BOARD)
    message(FATAL_ERROR
        "Build directory was configured for MYBOT_BOARD=${MYBOT_CONFIGURED_BOARD}; "
        "use a clean build directory for ${MYBOT_BOARD}")
endif()
set(MYBOT_CONFIGURED_BOARD "${MYBOT_BOARD}" CACHE INTERNAL "configured mybot board")

if(DEFINED IDF_TARGET AND NOT IDF_TARGET STREQUAL MYBOT_BOARD_TARGET)
    message(FATAL_ERROR
        "MYBOT_BOARD=${MYBOT_BOARD} requires IDF_TARGET=${MYBOT_BOARD_TARGET}, got ${IDF_TARGET}")
endif()
set(IDF_TARGET "${MYBOT_BOARD_TARGET}" CACHE STRING "ESP-IDF target")

if(NOT SDKCONFIG_DEFAULTS)
    if(DEFINED ENV{SDKCONFIG_DEFAULTS} AND NOT "$ENV{SDKCONFIG_DEFAULTS}" STREQUAL "")
        set(SDKCONFIG_DEFAULTS "$ENV{SDKCONFIG_DEFAULTS}")
    else()
        set(SDKCONFIG_DEFAULTS "")
    endif()
endif()
set(_mybot_sdkconfig_defaults ${SDKCONFIG_DEFAULTS})
set(_mybot_common_defaults "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults")
list(REMOVE_ITEM _mybot_sdkconfig_defaults "sdkconfig.defaults" "${_mybot_common_defaults}")
list(PREPEND _mybot_sdkconfig_defaults "${_mybot_common_defaults}")
list(FILTER _mybot_sdkconfig_defaults EXCLUDE REGEX
     "/components/mybot_platform/boards/[^/]+/sdkconfig\\.defaults$")
list(FIND _mybot_sdkconfig_defaults "${MYBOT_BOARD_SDKCONFIG_DEFAULTS}"
     _mybot_board_defaults_index)
if(_mybot_board_defaults_index EQUAL -1)
    list(APPEND _mybot_sdkconfig_defaults "${MYBOT_BOARD_SDKCONFIG_DEFAULTS}")
endif()
set(SDKCONFIG_DEFAULTS "${_mybot_sdkconfig_defaults}")

set(PROJECT_VER "0.1.0")

# Keep the MyBot runtime dependencies together while retaining their independent
# ESP-IDF component boundaries and dependency names.
set(MYBOT_STACK_ROOT "${CMAKE_CURRENT_LIST_DIR}/components/mybot_stack")
list(APPEND EXTRA_COMPONENT_DIRS
    "${MYBOT_STACK_ROOT}/mybot_sdk"
    "${MYBOT_STACK_ROOT}/aosl"
    "${MYBOT_STACK_ROOT}/agora_rtc"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
idf_build_set_property(MYBOT_BOARD "${MYBOT_BOARD}")

project(mybot_esp32s31)

# Apply the shared USB configuration after every component target exists.
if(MYBOT_BOARD STREQUAL "esp-mosaico-v1.2")
    include("${CMAKE_CURRENT_LIST_DIR}/components/mybot_platform/src/drivers/usb/mosaico_usb.cmake")
endif()
