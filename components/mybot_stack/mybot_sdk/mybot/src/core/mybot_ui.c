/* SPDX-License-Identifier: Apache-2.0 */
#include <mybot/mybot_ui.h>

const mybot_ui_language_t *mybot_ui_languages(size_t *count) {
    static const mybot_ui_language_t languages[] = {
        {"zh-CN", "中文", "Chinese"},
        {"en-US", "英语", "English"},
        {"ja-JP", "日语", "Japanese"},
        {"ko-KR", "韩语", "Korean"},
        {"es-ES", "西班牙语", "Spanish"},
        {"fr-FR", "法语", "French"},
        {"de-DE", "德语", "German"},
        {"it-IT", "意大利语", "Italian"},
        {"pt-BR", "葡萄牙语", "Portuguese"},
        {"ru-RU", "俄语", "Russian"},
        {"ar-SA", "阿拉伯语", "Arabic"},
        {"hi-IN", "印地语", "Hindi"},
        {"th-TH", "泰语", "Thai"},
        {"vi-VN", "越南语", "Vietnamese"},
        {"id-ID", "印度尼西亚语", "Indonesian"},
    };
    if (count)
        *count = sizeof(languages) / sizeof(languages[0]);
    return languages;
}
