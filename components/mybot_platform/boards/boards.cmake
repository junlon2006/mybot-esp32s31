# SPDX-License-Identifier: Apache-2.0
set(MYBOT_SUPPORTED_BOARDS
    "esp32-s31-korvo-1"
)
set(_MYBOT_BOARDS_ROOT "${CMAKE_CURRENT_LIST_DIR}")
function(mybot_resolve_board board)
    if(NOT board STREQUAL "esp32-s31-korvo-1")
        message(FATAL_ERROR "Unsupported MYBOT_BOARD: ${board}")
    endif()
    set(MYBOT_BOARD_PROFILE "${_MYBOT_BOARDS_ROOT}/${board}/board.cmake" PARENT_SCOPE)
endfunction()
