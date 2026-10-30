# SPDX-License-Identifier: Apache-2.0
set(AGORA_SDK_LIB "${COMPONENT_PATH}/lib/riscv32/libagora-rtc-sdk.a")
if(NOT EXISTS "${AGORA_SDK_LIB}" OR
   NOT EXISTS "${COMPONENT_PATH}/include/agora_rtc_api.h")
    message(FATAL_ERROR
        "Bundled S31 RTSA SDK is missing. Restore the pinned v1.10.1 files "
        "under components/mybot_stack/agora_rtc; see README.md.")
endif()

idf_component_register(INCLUDE_DIRS "${COMPONENT_PATH}/include"
                       REQUIRES aosl lwip)

# AOSL is built with WHOLE_ARCHIVE in its own component. Keep this archive a
# normal library so its bundled x86 host AOSL objects are never forced in.
target_link_libraries(${COMPONENT_LIB} INTERFACE "${AGORA_SDK_LIB}")
