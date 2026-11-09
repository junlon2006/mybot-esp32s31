/* SPDX-License-Identifier: MIT */
/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2020 Jerzy Kasenbreg
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#pragma once

#include "uac_descriptors.h"

/* Derived from usb_device_uac 1.3.1's duplex graph. USB IN has no feature
 * controls: its output terminal is connected directly to its input terminal.
 * Preserve every other field from the pinned, hardware-validated graph. */
#define MYBOT_MOSAICO_UAC_CONTROL_DESC_LEN                                                         \
    (TUD_AUDIO_DESC_CS_AC_TOTAL_LEN - TUD_AUDIO_DESC_MIC_FEATURE_UNIT_N_CHANNEL_LEN)
#define MYBOT_MOSAICO_UAC_DESC_LEN                                                                 \
    (TUD_AUDIO_DEVICE_DESC_LEN - TUD_AUDIO_DESC_MIC_FEATURE_UNIT_N_CHANNEL_LEN)

#define MYBOT_MOSAICO_UAC_DESCRIPTOR(_itfnum, _stridx, _epout, _epin, _epfb)                       \
    TUD_AUDIO_DESC_IAD(_itfnum, NUM_INTERFACES, 0x00),                                             \
        TUD_AUDIO_DESC_STD_AC(_itfnum, 0x00, _stridx),                                             \
        TUD_AUDIO_DESC_CS_AC(0x0200, AUDIO_FUNC_HEADSET, MYBOT_MOSAICO_UAC_CONTROL_DESC_LEN,       \
                             AUDIO_CS_AS_INTERFACE_CTRL_LATENCY_POS),                              \
        TUD_AUDIO_DESC_CLK_SRC(UAC2_ENTITY_CLOCK, 3, 7, 0x00, 0x00),                               \
        TUD_AUDIO_DESC_INPUT_TERM(UAC2_ENTITY_SPK_INPUT_TERMINAL, AUDIO_TERM_TYPE_USB_STREAMING,   \
                                  0x00, UAC2_ENTITY_CLOCK, SPEAK_CHANNEL_NUM,                      \
                                  AUDIO_CHANNEL_CONFIG_NON_PREDEFINED, 0x00,                       \
                                  (AUDIO_CTRL_R << AUDIO_IN_TERM_CTRL_CONNECTOR_POS), 0x00),       \
        TUD_AUDIO_DESC_FEATURE_UNIT_N_CHANNEL(TUD_AUDIO_DESC_SPK_FEATURE_UNIT_N_CHANNEL_LEN,       \
                                              UAC2_ENTITY_SPK_FEATURE_UNIT,                        \
                                              UAC2_ENTITY_SPK_INPUT_TERMINAL, 0x00, INPUT_CTRL),   \
        TUD_AUDIO_DESC_OUTPUT_TERM(UAC2_ENTITY_SPK_OUTPUT_TERMINAL,                                \
                                   AUDIO_TERM_TYPE_OUT_GENERIC_SPEAKER, 0x00,                      \
                                   UAC2_ENTITY_SPK_FEATURE_UNIT, UAC2_ENTITY_CLOCK, 0x0000, 0x00), \
        TUD_AUDIO_DESC_INPUT_TERM(UAC2_ENTITY_MIC_INPUT_TERMINAL, AUDIO_TERM_TYPE_IN_GENERIC_MIC,  \
                                  UAC2_ENTITY_MIC_OUTPUT_TERMINAL, UAC2_ENTITY_CLOCK,              \
                                  MIC_CHANNEL_NUM, AUDIO_CHANNEL_CONFIG_FRONT_CENTER, 0x00,        \
                                  (AUDIO_CTRL_R << AUDIO_IN_TERM_CTRL_CONNECTOR_POS), 0x00),       \
        TUD_AUDIO_DESC_OUTPUT_TERM(UAC2_ENTITY_MIC_OUTPUT_TERMINAL, AUDIO_TERM_TYPE_USB_STREAMING, \
                                   0x00, UAC2_ENTITY_MIC_INPUT_TERMINAL, UAC2_ENTITY_CLOCK,        \
                                   0x0000, 0x00),                                                  \
        TUD_AUDIO_DESC_STD_AS_INT(_itfnum + 1, 0x00, 0x00, _stridx + 1),                           \
        TUD_AUDIO_DESC_STD_AS_INT(_itfnum + 1, 0x01, 0x02, _stridx + 1),                           \
        TUD_AUDIO_DESC_CS_AS_INT(UAC2_ENTITY_SPK_INPUT_TERMINAL, AUDIO_CTRL_NONE,                  \
                                 AUDIO_FORMAT_TYPE_I, AUDIO_DATA_FORMAT_TYPE_I_PCM,                \
                                 SPEAK_CHANNEL_NUM, AUDIO_CHANNEL_CONFIG_NON_PREDEFINED, 0x00),    \
        TUD_AUDIO_DESC_TYPE_I_FORMAT(CFG_TUD_AUDIO_FUNC_1_FORMAT_1_N_BYTES_PER_SAMPLE_RX,          \
                                     CFG_TUD_AUDIO_FUNC_1_FORMAT_1_RESOLUTION_RX),                 \
        TUD_AUDIO_DESC_STD_AS_ISO_EP(                                                              \
            _epout, (TUSB_XFER_ISOCHRONOUS | TUSB_ISO_EP_ATT_ASYNCHRONOUS | TUSB_ISO_EP_ATT_DATA), \
            CFG_TUD_AUDIO_FUNC_1_FORMAT_1_EP_SZ_OUT, 1),                                           \
        TUD_AUDIO_DESC_CS_AS_ISO_EP(AUDIO_CS_AS_ISO_DATA_EP_ATT_NON_MAX_PACKETS_OK,                \
                                    AUDIO_CTRL_NONE,                                               \
                                    AUDIO_CS_AS_ISO_DATA_EP_LOCK_DELAY_UNIT_MILLISEC, 0x0001),     \
        TUD_AUDIO_DESC_STD_AS_ISO_FB_EP(_epfb, 4, 1),                                              \
        TUD_AUDIO_DESC_STD_AS_INT(_itfnum + 2, 0x00, 0x00, _stridx + 2),                           \
        TUD_AUDIO_DESC_STD_AS_INT(_itfnum + 2, 0x01, 0x01, _stridx + 2),                           \
        TUD_AUDIO_DESC_CS_AS_INT(UAC2_ENTITY_MIC_OUTPUT_TERMINAL, AUDIO_CTRL_NONE,                 \
                                 AUDIO_FORMAT_TYPE_I, AUDIO_DATA_FORMAT_TYPE_I_PCM,                \
                                 MIC_CHANNEL_NUM, AUDIO_CHANNEL_CONFIG_FRONT_CENTER, 0x00),        \
        TUD_AUDIO_DESC_TYPE_I_FORMAT(CFG_TUD_AUDIO_FUNC_1_FORMAT_1_N_BYTES_PER_SAMPLE_TX,          \
                                     CFG_TUD_AUDIO_FUNC_1_FORMAT_1_RESOLUTION_TX),                 \
        TUD_AUDIO_DESC_STD_AS_ISO_EP(                                                              \
            _epin, (TUSB_XFER_ISOCHRONOUS | TUSB_ISO_EP_ATT_ASYNCHRONOUS | TUSB_ISO_EP_ATT_DATA),  \
            CFG_TUD_AUDIO_FUNC_1_FORMAT_1_EP_SZ_IN, 1),                                            \
        TUD_AUDIO_DESC_CS_AS_ISO_EP(AUDIO_CS_AS_ISO_DATA_EP_ATT_NON_MAX_PACKETS_OK,                \
                                    AUDIO_CTRL_NONE,                                               \
                                    AUDIO_CS_AS_ISO_DATA_EP_LOCK_DELAY_UNIT_UNDEFINED, 0x0000)
