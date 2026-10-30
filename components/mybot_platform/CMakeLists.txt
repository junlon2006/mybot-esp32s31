idf_build_get_property(_mybot_selected_board MYBOT_BOARD)
if(NOT _mybot_selected_board)
    message(FATAL_ERROR "MYBOT_BOARD build property was not exported by the project")
endif()
set(MYBOT_BOARD "${_mybot_selected_board}")
include("${CMAKE_CURRENT_LIST_DIR}/boards/boards.cmake")
mybot_resolve_board("${MYBOT_BOARD}")
include("${MYBOT_BOARD_PROFILE}")

# Pass a fixed Kconfig file to IDF so menuconfig/confserver use the same board
# capability even when invoked separately from the CMake configure process.
set(mybot_display_kconfig "${CMAKE_CURRENT_LIST_DIR}/Kconfig.display")

# Decode project-owned Ogg assets on the build host; firmware needs no S3 decoder binary.
set(MYBOT_EMBEDDED_AUDIO_ASSETS "")
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    find_program(MYBOT_FFMPEG_EXECUTABLE ffmpeg REQUIRED)
    set(_prompt_output "${CMAKE_CURRENT_BINARY_DIR}/prompts/${CONFIG_MYBOT_LANGUAGE_TAG}")
    file(MAKE_DIRECTORY "${_prompt_output}")
    foreach(_asset prompt wificonfig 0 1 2 3 4 5 6 7 8 9)
        set(_input "${CMAKE_CURRENT_LIST_DIR}/assets/locales/${CONFIG_MYBOT_LANGUAGE_TAG}/${_asset}.ogg")
        set(_output "${_prompt_output}/${_asset}.pcm")
        if(NOT EXISTS "${_output}" OR "${_input}" IS_NEWER_THAN "${_output}")
            execute_process(COMMAND "${MYBOT_FFMPEG_EXECUTABLE}" -nostdin -y -v error
                -i "${_input}" -ac 1 -ar 16000 -f s16le "${_output}"
                RESULT_VARIABLE _decode_result)
            if(NOT _decode_result EQUAL 0)
                message(FATAL_ERROR "Failed to convert prompt: ${_input}")
            endif()
        endif()
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_input}")
        list(APPEND MYBOT_EMBEDDED_AUDIO_ASSETS "${_output}")
    endforeach()
endif()

if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    if(NOT CONFIG_MYBOT_PSRAM_ALLOCATIONS OR NOT CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM)
        message(FATAL_ERROR "mybot requires PSRAM allocations and external task stack support")
    endif()
    if(CONFIG_MYBOT_ENABLE_VIDEO)
        message(FATAL_ERROR "Video is not implemented in the ESP32-S31 first release")
    endif()
    foreach(required_config IN LISTS MYBOT_BOARD_REQUIRED_CONFIGS)
        if(NOT ${required_config})
            message(FATAL_ERROR
                "MYBOT_BOARD=${MYBOT_BOARD} requires ${required_config}=y; "
                "use a clean board-specific sdkconfig")
        endif()
    endforeach()
    foreach(forbidden_config IN LISTS MYBOT_BOARD_FORBIDDEN_CONFIGS)
        if(${forbidden_config})
            message(FATAL_ERROR
                "MYBOT_BOARD=${MYBOT_BOARD} requires ${forbidden_config}=n; "
                "use a clean board-specific sdkconfig")
        endif()
    endforeach()
    if(NOT CONFIG_PARTITION_TABLE_CUSTOM_FILENAME STREQUAL MYBOT_BOARD_PARTITION_TABLE)
        message(FATAL_ERROR
            "MYBOT_BOARD=${MYBOT_BOARD} requires partition table ${MYBOT_BOARD_PARTITION_TABLE}, "
            "got ${CONFIG_PARTITION_TABLE_CUSTOM_FILENAME}")
    endif()
endif()

set(mybot_debug_sources "")
if(CONFIG_MYBOT_DEBUG_RESOURCE_MONITOR)
    list(APPEND mybot_debug_sources "src/services/diagnostics/debug_stats.c")
endif()

idf_component_register(
    KCONFIG "${mybot_display_kconfig}"
    SRCS
        "src/platform/platform_register.c"
        "src/services/network/https.c"
        "src/services/network/conversation_http_trace.c"
        "src/services/storage/kv_store.c"
        "src/services/network/wifi.cpp"
        "src/services/announcement/embedded_pcm_assets.c"
        "src/services/audio/pcm_playback_buffer.c"
        ${MYBOT_BOARD_SOURCES}
        ${mybot_debug_sources}
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS
        "src/internal"
        ${MYBOT_BOARD_INCLUDE_DIR}
    REQUIRES
        mybot_sdk
        aosl
        esp-wifi-connect
        esp-tls
        esp_hw_support
        mbedtls
        nvs_flash
        freertos
        esp_timer
        lvgl
        esp_lvgl_port
        ${MYBOT_BOARD_REQUIRES}
    EMBED_FILES ${MYBOT_EMBEDDED_AUDIO_ASSETS}
    REQUIRED_IDF_TARGETS esp32s31
)

target_compile_features(${COMPONENT_LIB} PRIVATE c_std_99 cxx_std_17)
