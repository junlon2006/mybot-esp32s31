# SPDX-License-Identifier: Apache-2.0
set(MYBOT_SUPPORTED_BOARDS
    "esp-mosaico-v1.2"
    "esp32-s31-korvo-1"
)
set(_MYBOT_BOARDS_ROOT "${CMAKE_CURRENT_LIST_DIR}")
function(mybot_resolve_board board)
    if(NOT board IN_LIST MYBOT_SUPPORTED_BOARDS)
        message(FATAL_ERROR "Unsupported MYBOT_BOARD: ${board}")
    endif()
    set(MYBOT_BOARD_PROFILE "${_MYBOT_BOARDS_ROOT}/${board}/board.cmake" PARENT_SCOPE)
endfunction()
