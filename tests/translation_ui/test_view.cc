/* SPDX-License-Identifier: MIT */
#include "display/lvgl_view.h"
#include "input/korvo_s31_keys.h"
#include "network/wifi_control.h"
#include <mybot/mybot_ui.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #condition);                           \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (0)
struct lv_obj_t {
    lv_obj_t *parent{};
    std::vector<lv_obj_t *> children;
    std::string text;
    unsigned flags{}, state{};
    int x{}, y{}, width{}, height{}, value{}, event_code{};
    bool slider{};
    lv_event_cb_t callback{};
    void *user{};
};
struct lv_event_t {
    lv_obj_t *target;
    void *user;
    lv_event_code_t code;
};
struct lv_indev_t {
    lv_point_t point;
};
static lv_indev_t pointer;
struct lv_display_t {
    lv_obj_t root;
    int width = 800, height = 480;
};
struct lv_timer_t {
    lv_timer_cb_t callback;
};
extern const lv_font_t mybot_lvgl_font_20{};
extern const lv_font_t mybot_lvgl_font_28{};
extern const lv_font_t mybot_lvgl_font_40{};
static unsigned allocations, fail_at, live;
static uint32_t ticks;
static bool wifi_connected;
static bool accept_ui_commands = true;
static lv_timer_t *timer;
static std::vector<mybot_key_event_t> events;
static std::vector<mybot_ui_command_t> commands;

lv_obj_t *lv_obj_create(lv_obj_t *parent) {
    CHECK(parent);
    if (++allocations == fail_at)
        return nullptr;
    auto *object = new lv_obj_t;
    object->parent = parent;
    parent->children.push_back(object);
    ++live;
    return object;
}
lv_obj_t *lv_label_create(lv_obj_t *parent) {
    return lv_obj_create(parent);
}
void lv_obj_clean(lv_obj_t *object) {
    while (!object->children.empty())
        lv_obj_delete(object->children.back());
}
void lv_obj_delete(lv_obj_t *object) {
    if (object->callback && object->event_code == LV_EVENT_ALL) {
        lv_event_t event{object, object->user, LV_EVENT_DELETE};
        object->callback(&event);
    }
    lv_obj_clean(object);
    auto &siblings = object->parent->children;
    siblings.erase(std::find(siblings.begin(), siblings.end(), object));
    delete object;
    --live;
}
void lv_obj_remove_style_all(lv_obj_t *) {
}
void lv_obj_remove_flag(lv_obj_t *o, lv_obj_flag_t flags) {
    o->flags &= ~flags;
}
void lv_obj_add_flag(lv_obj_t *o, lv_obj_flag_t flags) {
    o->flags |= flags;
}
void lv_obj_set_pos(lv_obj_t *o, int x, int y) {
    o->x = x;
    o->y = y;
}
void lv_obj_set_size(lv_obj_t *o, int width, int height) {
    o->width = width;
    o->height = height;
}
void lv_obj_set_style_bg_color(lv_obj_t *, lv_color_t, int) {
}
void lv_obj_set_style_bg_opa(lv_obj_t *, int, int) {
}
void lv_obj_set_style_radius(lv_obj_t *, int, int) {
}
void lv_obj_set_style_text_font(lv_obj_t *, const lv_font_t *, int) {
}
void lv_obj_set_style_text_color(lv_obj_t *, lv_color_t, int) {
}
void lv_obj_set_style_text_align(lv_obj_t *, lv_text_align_t, int) {
}
void lv_obj_set_style_opa(lv_obj_t *, int, int) {
}
void lv_obj_add_event_cb(lv_obj_t *o, lv_event_cb_t cb, int code, void *user) {
    CHECK(code == LV_EVENT_CLICKED || code == LV_EVENT_ALL);
    o->slider = code == LV_EVENT_ALL;
    o->callback = cb;
    o->event_code = code;
    o->user = user;
}
lv_obj_t *lv_event_get_current_target_obj(lv_event_t *e) {
    return e->target;
}
void *lv_event_get_user_data(lv_event_t *e) {
    return e->user;
}
lv_event_code_t lv_event_get_code(lv_event_t *e) {
    return e->code;
}
lv_indev_t *lv_indev_active(void) {
    return &pointer;
}
void lv_indev_get_point(const lv_indev_t *input, lv_point_t *point) {
    *point = input->point;
}
void lv_obj_get_coords(const lv_obj_t *object, lv_area_t *area) {
    int x = 0, y = 0;
    for (const auto *parent = object; parent; parent = parent->parent) {
        x += parent->x;
        y += parent->y;
    }
    *area = {x, y, x + object->width - 1, y + object->height - 1};
}
void lv_obj_remove_state(lv_obj_t *o, unsigned state) {
    o->state &= ~state;
}
void lv_obj_add_state(lv_obj_t *o, unsigned state) {
    o->state |= state;
}
bool lv_obj_has_state(lv_obj_t *o, unsigned state) {
    return (o->state & state) != 0;
}
void lv_label_set_long_mode(lv_obj_t *, int) {
}
void lv_label_set_text(lv_obj_t *o, const char *text) {
    o->text = text;
}
lv_color_t lv_color_hex(uint32_t value) {
    return value;
}
lv_obj_t *lv_display_get_screen_active(lv_display_t *d) {
    return &d->root;
}
int lv_display_get_horizontal_resolution(lv_display_t *d) {
    return d->width;
}
int lv_display_get_vertical_resolution(lv_display_t *d) {
    return d->height;
}
lv_timer_t *lv_timer_create(lv_timer_cb_t callback, unsigned, void *) {
    CHECK(!timer);
    timer = new lv_timer_t{callback};
    return timer;
}
void lv_timer_delete(lv_timer_t *object) {
    CHECK(object == timer);
    delete timer;
    timer = nullptr;
}
uint32_t lv_tick_get(void) {
    return ticks;
}
extern "C" {
bool mybot_wifi_is_connected(void) {
    return wifi_connected;
}
void mybot_korvo_s31_keys_ui_event(mybot_key_event_t event) {
    events.push_back(event);
}
void mybot_korvo_s31_keys_ui_command(const mybot_ui_command_t *command) {
    commands.push_back(*command);
}
int mybot_ui_submit(const mybot_ui_command_t *command) {
    if (!accept_ui_commands)
        return -1;
    commands.push_back(*command);
    return 0;
}
int mybot_wifi_get_provisioning_ssid(char *out, size_t capacity) {
    std::snprintf(out, capacity, "Translation-WiFi");
    return 0;
}
}
#if CONFIG_MYBOT_LANGUAGE_ZH_CN
static const char *tr(const char *zh, const char *) {
    return zh;
}
#else
static const char *tr(const char *, const char *en) {
    return en;
}
#endif
static lv_obj_t *find(lv_obj_t *root, const char *text) {
    if (root->text == text)
        return root;
    for (auto *child : root->children)
        if (auto *result = find(child, text))
            return result;
    return nullptr;
}
static lv_obj_t *slider(lv_obj_t *root) {
    if (root->slider)
        return root;
    for (auto *child : root->children)
        if (auto *result = slider(child))
            return result;
    return nullptr;
}
static void click(lv_obj_t *object) {
    CHECK(object);
    while (object && !object->callback)
        object = object->parent;
    CHECK(object && object->event_code == LV_EVENT_CLICKED);
    const auto callback = object->callback;
    lv_event_t event{object, object->user, LV_EVENT_CLICKED};
    callback(&event);
}
static void pointer_event(lv_obj_t *object, lv_event_code_t code, int value = 37) {
    CHECK(object && object->event_code == LV_EVENT_ALL);
    lv_area_t area{};
    lv_obj_get_coords(object, &area);
    pointer.point = {area.x1 + 13 + ((object->width - 26) * value + 50) / 100, area.y1 + 18};
    lv_event_t event{object, object->user, code};
    object->callback(&event);
}
static void bounds(lv_obj_t *root) {
    for (auto *child : root->children) {
        CHECK(child->x >= 0 && child->y >= 0);
        CHECK(child->x + child->width <= root->width);
        CHECK(child->y + child->height <= root->height);
        bounds(child);
    }
}
int main() {
    lv_display_t display;
    display.root.width = 800;
    display.root.height = 480;
    display.width = 480;
    display.height = 320;
    CHECK(mybot_lvgl_view_create(&display) < 0);
    display.width = 800;
    display.height = 480;
    CHECK(mybot_lvgl_view_create(&display) == 0);
    const unsigned initial_allocations = allocations;
    mybot_lvgl_view_destroy();
    for (unsigned failure = 1; failure <= initial_allocations; ++failure) {
        allocations = 0;
        fail_at = failure;
        CHECK(mybot_lvgl_view_create(&display) < 0);
        mybot_lvgl_view_destroy();
        CHECK(!live && !timer && display.root.children.empty());
    }
    fail_at = 0;
    CHECK(mybot_lvgl_view_create(&display) == 0);
    mybot_lcd_content_t content{};
    content.screen = MYBOT_LCD_SCREEN_READY;
    wifi_connected = true;
    content.volume = 60;
    std::strcpy(content.source_language, "zh-CN");
    std::strcpy(content.target_language, "en-US");
    mybot_lvgl_view_update(&content);
    CHECK(find(&display.root, tr("Wi-Fi 已连接", "Wi-Fi connected")));
    CHECK(find(&display.root, tr("电量 --", "Battery --")));
    CHECK(find(&display.root, tr("输出音量 60%", "Output volume 60%")));
    bounds(&display.root);
    auto *volume = slider(&display.root);
    pointer_event(volume, LV_EVENT_PRESSED);
    pointer_event(volume, LV_EVENT_PRESSING);
    CHECK(commands.empty()); // Moving a slider does not issue commands before release.
    pointer_event(volume, LV_EVENT_RELEASED);
    CHECK(commands.size() == 1 && commands.back().type == MYBOT_UI_SET_VOLUME &&
          commands.back().value == 37);
    CHECK(volume->children.back()->x == (volume->width - 26) * 60 / 100); // Actual readback.
    pointer_event(volume, LV_EVENT_PRESSED, 80);
    pointer_event(volume, LV_EVENT_PRESS_LOST, 80);
    pointer_event(volume, LV_EVENT_RELEASED, 80);
    CHECK(commands.size() == 1); // Cancelled drags must never send a command.
    content.volume = 37;
    mybot_lvgl_view_update(&content);
    CHECK(find(&display.root, tr("输出音量 37%", "Output volume 37%")));
    click(find(&display.root, tr("开始\n翻译  >", "Let's\ntranslate  >")));
    bounds(&display.root);
    // Preview scenes can browse languages but cannot create or change a real session.
    click(find(&display.root, tr("面对面", "In person")));
    const auto command_count = commands.size();
    click(find(&display.root, tr("待开放", "Coming soon")));
    CHECK(events.empty());
    click(find(&display.root, tr("中文", "Chinese")));
    click(find(&display.root, tr("日语", "Japanese")));
    CHECK(commands.size() == command_count && events.empty());
    CHECK(find(&display.root, tr("日语", "Japanese")));
    click(find(&display.root, tr("< 返回", "< Back")));
    click(find(&display.root, tr("媒体翻译", "Listen")));
    click(find(&display.root, tr("中文", "Chinese")));
    click(find(&display.root, tr("英语", "English"))); // Other language is disabled.
    CHECK(commands.size() == command_count);
    click(find(&display.root, tr("下一页 >", "Next >")));
    click(find(&display.root, tr("下一页 >", "Next >")));
    bounds(&display.root);
    accept_ui_commands = false;
    click(find(&display.root, tr("印度尼西亚语", "Indonesian")));
    CHECK(commands.size() == command_count);
    CHECK(!lv_obj_has_state(find(&display.root, tr("中文", "Chinese"))->parent, LV_STATE_DISABLED));
    CHECK(!lv_obj_has_state(find(&display.root, tr("英语", "English"))->parent, LV_STATE_DISABLED));
    accept_ui_commands = true;
    click(find(&display.root, tr("中文", "Chinese")));
    click(find(&display.root, tr("下一页 >", "Next >")));
    click(find(&display.root, tr("下一页 >", "Next >")));
    click(find(&display.root, tr("印度尼西亚语", "Indonesian")));
    CHECK(commands.back().type == MYBOT_UI_SET_LANGUAGES);
    CHECK(std::strcmp(commands.back().source_language, "id-ID") == 0);
    CHECK(std::strcmp(commands.back().target_language, "en-US") == 0);
    const auto language_commands = commands.size();
    CHECK(lv_obj_has_state(find(&display.root, tr("中文", "Chinese"))->parent, LV_STATE_DISABLED));
    CHECK(lv_obj_has_state(find(&display.root, tr("英语", "English"))->parent, LV_STATE_DISABLED));
    click(find(&display.root, tr("英语", "English")));
    CHECK(!find(&display.root, tr("韩语", "Korean")));
    CHECK(commands.size() == language_commands);
    ticks += 2000;
    timer->callback(timer);
    content.volume = 38;
    mybot_lvgl_view_update(&content); // Unrelated readback cannot acknowledge the language pair.
    CHECK(lv_obj_has_state(find(&display.root, tr("英语", "English"))->parent, LV_STATE_DISABLED));
    click(find(&display.root, tr("英语", "English")));
    click(find(&display.root, tr("开始翻译  >", "Start translation  >")));
    CHECK(!find(&display.root, tr("韩语", "Korean")));
    CHECK(commands.size() == language_commands && events.empty());
    std::strcpy(content.source_language, "id-ID");
    mybot_lvgl_view_update(&content);
    CHECK(!lv_obj_has_state(find(&display.root, tr("英语", "English"))->parent, LV_STATE_DISABLED));
    click(find(&display.root, tr("英语", "English")));
    click(find(&display.root, tr("韩语", "Korean")));
    CHECK(commands.size() == language_commands + 1);
    CHECK(commands.back().type == MYBOT_UI_SET_LANGUAGES);
    CHECK(std::strcmp(commands.back().source_language, "id-ID") == 0);
    CHECK(std::strcmp(commands.back().target_language, "ko-KR") == 0);
    std::strcpy(content.target_language, "ko-KR");
    mybot_lvgl_view_update(&content);
    click(find(&display.root, tr("开始翻译  >", "Start translation  >")));
    CHECK(events.size() == 1 && events.back() == MYBOT_KEY_EVENT_CONVERSATION_START);
    CHECK(!find(&display.root, tr("演示字幕 · 固定示例", "Demo captions · Fixed example")));
    content.screen = MYBOT_LCD_SCREEN_TRANSLATION_STARTING;
    mybot_lvgl_view_update(&content);
    click(find(&display.root, tr("取消连接", "Cancel connection")));
    CHECK(events.back() == MYBOT_KEY_EVENT_CONVERSATION_STOP);
    CHECK(!find(&display.root, tr("翻译已结束", "Translation ended")));
    content.screen = MYBOT_LCD_SCREEN_IN_CONVERSATION;
    mybot_lvgl_view_update(&content);
    CHECK(!find(&display.root, tr("演示字幕 · 固定示例", "Demo captions · Fixed example")));
    click(find(&display.root, tr("重试结束", "Retry stop")));
    CHECK(events.back() == MYBOT_KEY_EVENT_CONVERSATION_STOP);
    content.screen = MYBOT_LCD_SCREEN_START_UNKNOWN;
    wifi_connected = false;
    mybot_lvgl_view_update(&content);
    CHECK(find(&display.root, tr("Wi-Fi 未连接", "Wi-Fi offline")));
    CHECK(find(&display.root, tr("启动结果未知", "Start outcome unknown")));
    CHECK(!find(&display.root, tr("开始翻译  >", "Start translation  >")));
    const auto locked_events = events.size();
    click(find(&display.root, tr("请稍候", "Please wait")));
    CHECK(events.size() == locked_events);
    content.screen = MYBOT_LCD_SCREEN_CLEANUP_PENDING;
    std::strcpy(content.error_code, "cloud_stop_failed");
    mybot_lvgl_view_update(&content);
    CHECK(find(&display.root, "cloud_stop_failed"));
    CHECK(find(&display.root, tr("结束尚未完成", "Cleanup pending")));
    click(find(&display.root, tr("重试结束", "Retry stop")));
    CHECK(events.size() == locked_events + 1 && events.back() == MYBOT_KEY_EVENT_CONVERSATION_STOP);
    content.screen = MYBOT_LCD_SCREEN_TRANSLATION_STOPPING;
    mybot_lvgl_view_update(&content);
    CHECK(find(&display.root, tr("正在结束翻译", "Ending translation")));
    CHECK(!find(&display.root, tr("翻译已结束", "Translation ended")));
    content.screen = MYBOT_LCD_SCREEN_READY;
    wifi_connected = true;
    std::strcpy(content.error_code, "CLEANUP_AUTH_REJECTED"); // Only READY confirms cleanup.
    mybot_lvgl_view_update(&content);
    CHECK(find(&display.root, tr("开始翻译  >", "Start translation  >")));
    CHECK(!find(&display.root, tr("翻译已结束", "Translation ended"))); // Cancel returns to setup.
    CHECK(!find(&display.root, "CLEANUP_AUTH_REJECTED"));
    content.error_code[0] = 0;
    mybot_lvgl_view_update(&content);
    click(find(&display.root, tr("开始翻译  >", "Start translation  >")));
    content.screen = MYBOT_LCD_SCREEN_IN_CONVERSATION;
    mybot_lvgl_view_update(&content);
    CHECK(find(&display.root, tr("演示字幕 · 固定示例", "Demo captions · Fixed example")));
    bounds(&display.root);
    click(find(&display.root, tr("暂停", "Pause")));
    CHECK(commands.back().type == MYBOT_UI_SET_PAUSED && commands.back().value == 1);
    CHECK(!find(&display.root,
                tr("继续", "Resume"))); // Only SDK acknowledgement changes pause state.
    const size_t pending_pause_commands = commands.size();
    click(find(&display.root, tr("暂停", "Pause")));
    CHECK(commands.size() == pending_pause_commands); // Guard repeated taps before acknowledgement.
    ticks += 2000;
    timer->callback(timer); // A dropped mailbox message must permit retry.
    click(find(&display.root, tr("暂停", "Pause")));
    CHECK(commands.size() == pending_pause_commands + 1);
    mybot_lvgl_view_update(&content); // An unchanged readback also releases the local guard.
    click(find(&display.root, tr("暂停", "Pause")));
    CHECK(commands.size() == pending_pause_commands + 2);
    content.paused = true;
    mybot_lvgl_view_update(&content);
    CHECK(find(&display.root, tr("继续", "Resume")));
    click(find(&display.root, "We can start the meeting now."));
    CHECK(find(&display.root, tr("固定演示内容，与实时音频无关",
                                 "Fixed demo content; independent of live audio")));
    bounds(&display.root);
    click(find(&display.root, tr("返回实时翻译", "Back to live")));
    click(find(&display.root, tr("更多 ...", "More ...")));
    click(find(&display.root, tr("本次字幕与全文  >", "Captions and full text  >")));
    CHECK(find(&display.root, tr("演示字幕历史", "Demo caption history")));
    click(find(&display.root, tr("返回实时翻译", "Back to live")));
    click(find(&display.root, tr("更多 ...", "More ...")));
    click(find(&display.root, tr("结束这次翻译", "End this translation")));
    const auto before_end = events.size();
    click(find(&display.root, tr("继续翻译", "Keep translating")));
    CHECK(events.size() == before_end);
    click(find(&display.root, tr("更多 ...", "More ...")));
    click(find(&display.root, tr("结束这次翻译", "End this translation")));
    click(find(&display.root, tr("结束翻译", "End translation")));
    CHECK(events.size() == before_end + 1 && events.back() == MYBOT_KEY_EVENT_CONVERSATION_STOP);
    CHECK(!find(&display.root, tr("翻译已结束", "Translation ended")));
    CHECK(find(&display.root, tr("正在结束翻译", "Ending translation")));
    content.screen = MYBOT_LCD_SCREEN_CLEANUP_PENDING;
    std::strcpy(content.error_code, "CLEANUP_PENDING");
    mybot_lvgl_view_update(&content);
    CHECK(find(&display.root, tr("结束尚未完成", "Cleanup pending")));
    CHECK(!find(&display.root, tr("无法开始翻译", "Could not start translation")));
    CHECK(!find(&display.root, tr("翻译已结束", "Translation ended")));
    click(find(&display.root, tr("重试结束", "Retry stop")));
    CHECK(events.size() == before_end + 2 && events.back() == MYBOT_KEY_EVENT_CONVERSATION_STOP);
    content.screen = MYBOT_LCD_SCREEN_TRANSLATION_STOPPING;
    mybot_lvgl_view_update(&content);
    CHECK(find(&display.root, tr("正在结束翻译", "Ending translation")));
    CHECK(!find(&display.root, tr("翻译已结束", "Translation ended")));
    content.screen = MYBOT_LCD_SCREEN_READY;
    mybot_lvgl_view_update(&content);
    CHECK(find(&display.root, tr("翻译已结束", "Translation ended")));
    CHECK(!find(&display.root, "CLEANUP_PENDING"));
    mybot_lvgl_view_update(&content); // A repeated stale hint must not replace completion.
    CHECK(find(&display.root, tr("翻译已结束", "Translation ended")));
    // A normal end without a failed stop still uses the SDK acknowledgement.
    content.error_code[0] = 0;
    mybot_lvgl_view_update(&content);
    click(find(&display.root, tr("再来一次", "Translate again")));
    click(find(&display.root, tr("开始翻译  >", "Start translation  >")));
    content.screen = MYBOT_LCD_SCREEN_IN_CONVERSATION;
    mybot_lvgl_view_update(&content);
    click(find(&display.root, tr("更多 ...", "More ...")));
    click(find(&display.root, tr("结束这次翻译", "End this translation")));
    click(find(&display.root, tr("结束翻译", "End translation")));
    content.screen = MYBOT_LCD_SCREEN_TRANSLATION_STOPPING;
    mybot_lvgl_view_update(&content);
    content.screen = MYBOT_LCD_SCREEN_READY;
    mybot_lvgl_view_update(&content);
    CHECK(find(&display.root, tr("翻译已结束", "Translation ended")));
    // Changing settings, unlike a regular end, returns to setup after cleanup.
    click(find(&display.root, tr("再来一次", "Translate again")));
    click(find(&display.root, tr("开始翻译  >", "Start translation  >")));
    content.screen = MYBOT_LCD_SCREEN_IN_CONVERSATION;
    mybot_lvgl_view_update(&content);
    click(find(&display.root, tr("更多 ...", "More ...")));
    click(find(&display.root, tr("更改语言与使用方式  >", "Change languages and scene  >")));
    click(find(&display.root, tr("结束并更换", "End and change")));
    content.screen = MYBOT_LCD_SCREEN_TRANSLATION_STOPPING;
    mybot_lvgl_view_update(&content);
    content.screen = MYBOT_LCD_SCREEN_READY;
    std::strcpy(content.error_code, "CLEANUP_PENDING");
    mybot_lvgl_view_update(&content);
    CHECK(find(&display.root, tr("开始翻译  >", "Start translation  >")));
    CHECK(!find(&display.root, tr("翻译已结束", "Translation ended")));
    for (const char *error : {"MEDIA_NOT_READY", "RTC_NOT_READY", "CONVERSATION_START_FAILED",
                              "TIME_NOT_SYNCED", "STORAGE_ERROR"}) {
        content.screen = MYBOT_LCD_SCREEN_READY;
        std::strcpy(content.error_code, error);
        mybot_lvgl_view_update(&content);
        CHECK(find(&display.root, error));
        if (std::strcmp(error, "RTC_NOT_READY") == 0) {
            CHECK(find(&display.root, tr("本地连接未恢复", "Local RTC is not ready")));
            CHECK(find(&display.root,
                       tr("请重试或重启设备", "Retry, or restart the device if this persists.")));
        }
        CHECK(!find(&display.root, tr("启动结果未知", "Start outcome unknown")));
        click(find(&display.root, tr("返回设置", "Back to settings")));
        CHECK(find(&display.root, tr("开始翻译  >", "Start translation  >")));
        click(find(&display.root, tr("开始翻译  >", "Start translation  >")));
        mybot_lvgl_view_update(&content); // Repeated same-state rejection stays visible.
        CHECK(find(&display.root, error));
    }
    // Updating a READY error screen with a stale cleanup hint must leave no wait screen.
    std::strcpy(content.error_code, "CLEANUP_PENDING");
    mybot_lvgl_view_update(&content);
    CHECK(find(&display.root, tr("开始翻译  >", "Start translation  >")));
    CHECK(!find(&display.root, tr("请稍候", "Please wait")));
    CHECK(!find(&display.root, tr("无法开始翻译", "Could not start translation")));
    content.screen = MYBOT_LCD_SCREEN_TRANSLATION_ERROR;
    std::strcpy(content.error_code, "MEDIA_NOT_READY");
    mybot_lvgl_view_update(&content);
    CHECK(find(&display.root,
               tr("本地音频未恢复，请重试", "Local audio has not recovered. Please retry.")));
    click(find(&display.root, tr("返回设置", "Back to settings")));
    click(find(&display.root, tr("韩语", "Korean")));
    click(find(&display.root, tr("日语", "Japanese")));
    content.volume = 39;
    mybot_lvgl_view_update(&content); // An unchanged old error is not a language result.
    CHECK(lv_obj_has_state(find(&display.root, tr("韩语", "Korean"))->parent, LV_STATE_DISABLED));
    std::strcpy(content.target_language, "ja-JP");
    content.error_code[0] = 0;
    mybot_lvgl_view_update(&content);
    CHECK(
        !lv_obj_has_state(find(&display.root, tr("日语", "Japanese"))->parent, LV_STATE_DISABLED));
    content.screen = MYBOT_LCD_SCREEN_READY;
    content.error_code[0] = 0;
    mybot_lvgl_view_update(&content);
    click(find(&display.root, tr("开始\n翻译  >", "Let's\ntranslate  >")));
    click(find(&display.root, tr("媒体翻译", "Listen")));
    click(find(&display.root, tr("日语", "Japanese")));
    click(find(&display.root, tr("韩语", "Korean")));
    CHECK(lv_obj_has_state(find(&display.root, tr("日语", "Japanese"))->parent, LV_STATE_DISABLED));
    content.screen = MYBOT_LCD_SCREEN_TRANSLATION_ERROR;
    std::strcpy(content.error_code, "STORAGE_ERROR");
    mybot_lvgl_view_update(&content); // A failed save must release the language guard too.
    click(find(&display.root, tr("返回设置", "Back to settings")));
    CHECK(
        !lv_obj_has_state(find(&display.root, tr("日语", "Japanese"))->parent, LV_STATE_DISABLED));
    content.screen = MYBOT_LCD_SCREEN_PAIR_CODE;
    std::strcpy(content.pair_code, "654321");
    mybot_lvgl_view_update(&content);
    CHECK(find(&display.root, "654321"));
    content.screen = MYBOT_LCD_SCREEN_WIFI_PROVISIONING;
    wifi_connected = false;
    mybot_lvgl_view_update(&content);
    CHECK(find(&display.root, "Translation-WiFi"));
    CHECK(find(&display.root, tr("Wi-Fi 未连接", "Wi-Fi offline")));
    timer->callback(timer);
    mybot_lvgl_view_destroy();
    mybot_lvgl_view_destroy();
    CHECK(!live && !timer && display.root.children.empty());
    std::puts("PASS native simulator UI: scene previews, direct language selection, real volume, "
              "pause, demos, lifecycle gates and allocation cleanup");
}
