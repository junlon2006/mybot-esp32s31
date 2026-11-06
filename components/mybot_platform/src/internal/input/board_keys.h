/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "board_config.h"
#if defined(MYBOT_BOARD_IS_MOSAICO) && MYBOT_BOARD_IS_MOSAICO
#include "input/mosaico_keys.h"
#define mybot_board_keys_ui_event mybot_mosaico_keys_ui_event
#define mybot_board_keys_ui_command mybot_mosaico_keys_ui_command
#else
#include "input/korvo_s31_keys.h"
#define mybot_board_keys_ui_event mybot_korvo_s31_keys_ui_event
#define mybot_board_keys_ui_command mybot_korvo_s31_keys_ui_command
#endif
