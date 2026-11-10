/* SPDX-License-Identifier: MIT */
#include "display/lvgl_view.h"
#include "input/ui_input.h"
#include "network/wifi_control.h"
#include <mybot/mybot_ui.h>
#include <mybot/mybot_captions.h>

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
    unsigned flags{}, state{}, background{};
    int x{}, y{}, width{}, height{}, value{}, event_code{}, long_mode{};
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
    int width = 480, height = 480;
};
struct lv_timer_t {
    lv_timer_cb_t callback;
};
extern const lv_font_t mybot_lvgl_font_20{};
extern const lv_font_t mybot_lvgl_font_28{};
extern const lv_font_t mybot_lvgl_font_40{};
extern const lv_font_t mybot_caption_font_20{};
extern const lv_font_t mybot_caption_font_28{};
static unsigned allocations, fail_at, live;
static uint32_t ticks;
static bool wifi_connected;
static bool accept_ui_commands = true;
static lv_timer_t *timer;
static std::vector<mybot_key_event_t> events;
static std::vector<mybot_ui_command_t> commands;
static std::vector<mybot_caption_t> captions;
static uint32_t caption_generation = 1;
static bool captions_busy;
static unsigned caption_reads;

void *lv_malloc(size_t size) {
    if (++allocations == fail_at)
        return nullptr;
    void *memory = std::malloc(size);
    if (memory)
        ++live;
    return memory;
}
void lv_free(void *memory) {
    CHECK(memory);
    std::free(memory);
    --live;
}

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
    o->height = height == LV_SIZE_CONTENT ? 28 : height;
}
void lv_obj_set_scroll_dir(lv_obj_t *, int) {
}
void lv_obj_set_flex_flow(lv_obj_t *, int) {
}
void lv_obj_set_style_pad_all(lv_obj_t *, int, int) {
}
void lv_obj_set_style_pad_row(lv_obj_t *, int, int) {
}
void lv_obj_set_style_bg_color(lv_obj_t *object, lv_color_t color, int) {
    object->background = color;
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
void lv_label_set_long_mode(lv_obj_t *object, int mode) {
    object->long_mode = mode;
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
int mybot_ui_copy_caption(uint32_t generation, uint32_t sequence, mybot_caption_t *out) {
    ++caption_reads;
    if (captions_busy || generation != caption_generation)
        return -1;
    for (const auto &caption : captions)
        if (caption.sequence == sequence) {
            *out = caption;
            return 0;
        }
    return -1;
}
bool mybot_wifi_is_connected(void) {
    return wifi_connected;
}
void mybot_ui_input_event(mybot_key_event_t event) {
    events.push_back(event);
}
int mybot_ui_submit(const mybot_ui_command_t *command) {
    if (!accept_ui_commands)
        return -1;
    commands.push_back(*command);
    return 0;
}
int mybot_ui_input_command(const mybot_ui_command_t *command) {
    return mybot_ui_submit(command);
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
static const char *home_action() {
    return tr("开始翻译 >", "Start translation >");
}
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
static void bounds(lv_obj_t *root);
static void click(lv_obj_t *object) {
    auto *root = object;
    while (root && root->parent)
        root = root->parent;
    CHECK(object);
    while (object && !object->callback)
        object = object->parent;
    CHECK(object && object->event_code == LV_EVENT_CLICKED);
    const auto callback = object->callback;
    lv_event_t event{object, object->user, LV_EVENT_CLICKED};
    callback(&event);
    bounds(root);
}
static void pointer_event(lv_obj_t *object, lv_event_code_t code, int value = 37) {
    CHECK(object && object->event_code == LV_EVENT_ALL);
    lv_area_t area{};
    lv_obj_get_coords(object, &area);
    pointer.point = {area.x1 + 13 + ((object->width - 26) * value + 50) / 100, area.y1 + 18};
    lv_event_t event{object, object->user, code};
    object->callback(&event);
}
static bool rounded_screen_contains(int x, int y) {
    constexpr int radius = 64;
    if ((x >= radius && x < 480 - radius) || (y >= radius && y < 480 - radius))
        return true;
    const int cx = x < radius ? radius : 480 - radius - 1;
    const int cy = y < radius ? radius : 480 - radius - 1;
    return (x - cx) * (x - cx) + (y - cy) * (y - cy) <= radius * radius;
}
static void bounds(lv_obj_t *root) {
    auto *screen = root;
    while (screen->parent)
        screen = screen->parent;
    for (auto *child : root->children) {
        CHECK(child->x >= 0 && child->y >= 0);
        CHECK(child->x + child->width <= root->width);
        CHECK(child->y + child->height <= root->height);
        if (screen->width == 480 &&
            (!child->text.empty() || (child->flags & LV_OBJ_FLAG_CLICKABLE))) {
            lv_area_t area{};
            lv_obj_get_coords(child, &area);
            CHECK(rounded_screen_contains(area.x1, area.y1));
            CHECK(rounded_screen_contains(area.x2, area.y1));
            CHECK(rounded_screen_contains(area.x1, area.y2));
            CHECK(rounded_screen_contains(area.x2, area.y2));
            if (area.y1 < 52) {
                CHECK(area.x1 >= 60 && area.x2 < 420);
            }
        }
        if (child->flags & LV_OBJ_FLAG_CLICKABLE) {
            CHECK(child->width >= 44 && child->height >= 44);
            for (auto *sibling : root->children) {
                if (sibling == child || !(sibling->flags & LV_OBJ_FLAG_CLICKABLE))
                    continue;
                CHECK(child->x + child->width <= sibling->x ||
                      sibling->x + sibling->width <= child->x ||
                      child->y + child->height <= sibling->y ||
                      sibling->y + sibling->height <= child->y);
            }
        }
        bounds(child);
    }
}
static void update(lv_display_t &display, const mybot_lcd_content_t &content) {
    mybot_lvgl_view_update(&content);
    bounds(&display.root);
}
static void test_face_flow() {
    lv_display_t display;
    display.width = display.root.width = 480;
    display.root.height = 480;
    commands.clear();
    events.clear();
    captions.clear();
    ++caption_generation;
    CHECK(mybot_lvgl_view_create(&display) == 0);
    mybot_lcd_content_t content{};
    content.screen = MYBOT_LCD_SCREEN_READY;
    content.volume = 40;
    content.caption_generation = caption_generation;
    std::strcpy(content.source_language, "zh-CN");
    std::strcpy(content.target_language, "en-US");
    update(display, content);
    click(find(&display.root, home_action()));
    accept_ui_commands = false;
    click(find(&display.root, tr("面对面", "In person")));
    CHECK(commands.empty());
    CHECK(find(&display.root, tr("选择翻译方式", "Choose a translation scene")));
    accept_ui_commands = true;
    click(find(&display.root, tr("面对面", "In person")));
    CHECK(commands.size() == 1 && commands.back().type == MYBOT_UI_SET_MODE &&
          commands.back().value == MYBOT_TRANSLATION_LISTEN_SPEAK);
    CHECK(lv_obj_has_state(find(&display.root, tr("中文", "Chinese"))->parent, LV_STATE_DISABLED));
    CHECK(lv_obj_has_state(find(&display.root, tr("开始翻译  >", "Start translation  >"))->parent,
                           LV_STATE_DISABLED));
    ++content.volume;
    ++content.caption_revision;
    update(display, content);
    ticks += 2000;
    timer->callback(timer);
    click(find(&display.root, tr("开始翻译  >", "Start translation  >")));
    CHECK(events.empty()); // Volume, caption updates and timeouts do not acknowledge mode.
    std::strcpy(content.error_code, "MODE_CHANGE_FAILED");
    ++content.selection_revision;
    update(display, content);
    CHECK(find(&display.root, "MODE_CHANGE_FAILED"));
    click(find(&display.root, tr("返回设置", "Back to settings")));
    click(find(&display.root, tr("< 返回", "< Back")));
    click(find(&display.root, tr("面对面", "In person")));
    ++content.volume;
    update(display, content); // A prior failed mode change cannot unlock the next request.
    CHECK(lv_obj_has_state(find(&display.root, tr("中文", "Chinese"))->parent, LV_STATE_DISABLED));
    ++content.selection_revision;
    update(display, content);
    CHECK(find(&display.root, "MODE_CHANGE_FAILED")); // A repeated failure remains visible.
    click(find(&display.root, tr("返回设置", "Back to settings")));
    click(find(&display.root, tr("< 返回", "< Back")));
    click(find(&display.root, tr("面对面", "In person")));
    content.error_code[0] = 0;
    content.translation_mode = MYBOT_TRANSLATION_LISTEN_SPEAK;
    ++content.selection_revision;
    update(display, content);
    CHECK(!lv_obj_has_state(find(&display.root, tr("中文", "Chinese"))->parent, LV_STATE_DISABLED));
    click(find(&display.root, tr("中文", "Chinese")));
    CHECK(find(&display.root, tr("我的语言", "My language")));
    click(find(&display.root, tr("日语", "Japanese")));
    CHECK(commands.back().type == MYBOT_UI_SET_LANGUAGES &&
          std::strcmp(commands.back().source_language, "ja-JP") == 0);
    std::strcpy(content.source_language, "ja-JP");
    update(display, content);
    click(find(&display.root, tr("日语", "Japanese")));
    click(find(&display.root, tr("中文", "Chinese")));
    std::strcpy(content.source_language, "zh-CN");
    update(display, content);
    click(find(&display.root, tr("开始翻译  >", "Start translation  >")));
    CHECK(events.back() == MYBOT_KEY_EVENT_CONVERSATION_START);
    content.screen = MYBOT_LCD_SCREEN_TRANSLATION_STARTING;
    update(display, content);
    content.screen = MYBOT_LCD_SCREEN_IN_CONVERSATION;
    update(display, content);
    CHECK(find(&display.root, tr("面对面翻译", "Face-to-face translation")));
    CHECK(find(&display.root, tr("请选择发言语言", "Choose the speaking language")));
    CHECK(find(&display.root, tr("麦克风尚未开启", "Microphone input is off")));
    CHECK(lv_obj_has_state(find(&display.root, tr("暂停", "Pause"))->parent, LV_STATE_DISABLED));
    bounds(&display.root);
    const size_t before_direction = commands.size();
    accept_ui_commands = false;
    click(find(&display.root, tr("中文", "Chinese")));
    CHECK(commands.size() == before_direction);
    CHECK(!lv_obj_has_state(find(&display.root, tr("英语", "English"))->parent, LV_STATE_DISABLED));
    accept_ui_commands = true;
    click(find(&display.root, tr("中文", "Chinese")));
    CHECK(commands.size() == before_direction + 1 &&
          commands.back().type == MYBOT_UI_SET_DIRECTION &&
          commands.back().value == MYBOT_TRANSLATION_DIRECTION_SPEAK);
    CHECK(lv_obj_has_state(find(&display.root, tr("英语", "English"))->parent, LV_STATE_DISABLED));
    CHECK(find(&display.root, tr("中文", "Chinese"))->parent->background != 0x8ee3df);
    ++content.volume;
    ++content.caption_revision;
    update(display, content);
    ticks += 2000;
    timer->callback(timer);
    click(find(&display.root, tr("英语", "English")));
    CHECK(commands.size() == before_direction + 1);
    CHECK(find(&display.root, tr("麦克风尚未开启", "Microphone input is off")));
    content.input_direction = MYBOT_TRANSLATION_DIRECTION_SPEAK;
    ++content.selection_revision;
    update(display, content);
    CHECK(find(&display.root, tr("中文", "Chinese"))->parent->background == 0x8ee3df);
    CHECK(find(&display.root, tr("英语", "English"))->parent->background != 0x8ee3df);
    CHECK(!lv_obj_has_state(find(&display.root, tr("暂停", "Pause"))->parent, LV_STATE_DISABLED));
    click(find(&display.root, tr("中文", "Chinese")));
    CHECK(commands.size() == before_direction + 1); // Same active direction has no side effect.

    mybot_caption_t speak{};
    speak.sequence = 1;
    speak.direction = MYBOT_TRANSLATION_DIRECTION_SPEAK;
    std::strcpy(speak.source_text, "请问地铁站在哪里？");
    std::strcpy(speak.target_text, "Where is the metro station?");
    captions.push_back(speak);
    mybot_caption_t listen{};
    listen.sequence = 2;
    listen.direction = MYBOT_TRANSLATION_DIRECTION_LISTEN;
    std::strcpy(listen.source_text, "Turn right at the next corner.");
    std::strcpy(listen.target_text, "在下一个路口右转。");
    captions.push_back(listen);
    content.caption_count = content.caption_latest_sequence = 2;
    content.caption_first_sequence = 1;
    content.caption_latest_speak_sequence = 1;
    content.caption_latest_listen_sequence = 2;
    ++content.caption_revision;
    update(display, content);
    auto *speaking_label = find(&display.root, speak.target_text);
    CHECK(speaking_label && !find(&display.root, listen.target_text));
    listen.sequence = 3;
    std::strcpy(listen.target_text, "到路口后向右转。");
    captions.push_back(listen);
    content.caption_count = content.caption_latest_sequence =
        content.caption_latest_listen_sequence = 3;
    ++content.caption_revision;
    const unsigned before_other_caption = allocations;
    update(display, content);
    CHECK(find(&display.root, speak.target_text) == speaking_label);
    CHECK(!find(&display.root, listen.target_text) && allocations == before_other_caption);
    click(find(&display.root, tr("英语", "English")));
    CHECK(commands.back().type == MYBOT_UI_SET_DIRECTION &&
          commands.back().value == MYBOT_TRANSLATION_DIRECTION_LISTEN);
    CHECK(find(&display.root, speak.target_text)); // Old direction remains authoritative until ack.
    content.input_direction = MYBOT_TRANSLATION_DIRECTION_LISTEN;
    ++content.selection_revision;
    update(display, content);
    CHECK(find(&display.root, listen.target_text) && !find(&display.root, speak.target_text));
    CHECK(find(&display.root, tr("英语", "English"))->parent->background == 0x8ee3df);
    click(find(&display.root, tr("暂停", "Pause")));
    CHECK(commands.back().type == MYBOT_UI_SET_PAUSED && commands.back().value == 1);
    content.paused = true;
    update(display, content);
    click(find(&display.root, tr("英语", "English")));
    CHECK(commands.back().type == MYBOT_UI_SET_DIRECTION &&
          commands.back().value == MYBOT_TRANSLATION_DIRECTION_LISTEN);
    ++content.volume;
    update(display, content);
    CHECK(lv_obj_has_state(find(&display.root, tr("中文", "Chinese"))->parent, LV_STATE_DISABLED));
    content.paused = false;
    ++content.selection_revision;
    update(display, content);
    CHECK(!lv_obj_has_state(find(&display.root, tr("中文", "Chinese"))->parent, LV_STATE_DISABLED));

    click(find(&display.root, tr("中文", "Chinese")));
    content.paused = true;
    std::strcpy(content.error_code, "DIRECTION_CHANGE_FAILED");
    ++content.selection_revision;
    update(display, content);
    CHECK(find(&display.root, tr("切换失败，请重试", "Could not switch. Retry.")));
    CHECK(!lv_obj_has_state(find(&display.root, tr("中文", "Chinese"))->parent, LV_STATE_DISABLED));
    click(find(&display.root, tr("中文", "Chinese")));
    ++content.caption_revision;
    ++content.volume;
    update(display, content); // An old failure code does not complete the retry.
    CHECK(lv_obj_has_state(find(&display.root, tr("英语", "English"))->parent, LV_STATE_DISABLED));
    ++content.selection_revision; // A repeated same-code failure still releases this attempt.
    update(display, content);
    CHECK(!lv_obj_has_state(find(&display.root, tr("英语", "English"))->parent, LV_STATE_DISABLED));
    click(find(&display.root, tr("中文", "Chinese")));
    content.input_direction = MYBOT_TRANSLATION_DIRECTION_SPEAK;
    content.paused = false;
    content.error_code[0] = 0;
    ++content.selection_revision;
    update(display, content);
    CHECK(find(&display.root, speak.target_text));

    click(find(&display.root, tr("更多 ...", "More ...")));
    click(find(&display.root, tr("输出音量", "Output volume")));
    CHECK(slider(&display.root));
    CHECK(!find(&display.root, tr("亮度 · 演示", "Brightness · Demo")));
    click(find(&display.root, tr("< 返回", "< Back")));
    click(find(&display.root, tr("更多 ...", "More ...")));
    click(find(&display.root, tr("本次字幕与全文  >", "Captions and full text  >")));
    CHECK(find(&display.root, tr("中文  >  英语", "Chinese  >  English")));
    CHECK(find(&display.root, tr("英语  >  中文", "English  >  Chinese")));
    click(find(&display.root, listen.target_text));
    CHECK(find(&display.root, tr("英语  >  中文", "English  >  Chinese")));
    CHECK(!find(&display.root, tr("中文  >  英语", "Chinese  >  English")));
    speak.sequence = 4;
    std::strcpy(speak.target_text, "Thanks for your help.");
    captions.push_back(speak);
    content.caption_count = content.caption_latest_sequence =
        content.caption_latest_speak_sequence = 4;
    ++content.caption_revision;
    const unsigned full_allocations = allocations;
    update(display, content);
    CHECK(find(&display.root, listen.target_text));
    CHECK(!find(&display.root, speak.target_text) && allocations == full_allocations);
    click(find(&display.root, tr("返回实时翻译", "Back to live")));
    CHECK(find(&display.root, speak.target_text));
    click(find(&display.root, tr("更多 ...", "More ...")));
    click(find(&display.root, tr("结束这次翻译", "End this translation")));
    click(find(&display.root, tr("结束翻译", "End translation")));
    CHECK(events.back() == MYBOT_KEY_EVENT_CONVERSATION_STOP);
    content.screen = MYBOT_LCD_SCREEN_READY;
    update(display, content);
    CHECK(find(&display.root, tr("翻译已结束", "Translation ended")));
    CHECK(find(&display.root, tr("< 返回", "< Back")));
    click(find(&display.root, tr("查看本次字幕", "View captions")));
    CHECK(find(&display.root, tr("英语  >  中文", "English  >  Chinese")));
    click(find(&display.root, tr("返回结束页", "Back to summary")));
    click(find(&display.root, tr("再来一次", "Translate again")));
    click(find(&display.root, tr("< 返回", "< Back")));
    click(find(&display.root, tr("媒体翻译", "Listen")));
    CHECK(commands.back().type == MYBOT_UI_SET_MODE &&
          commands.back().value == MYBOT_TRANSLATION_LISTEN);
    CHECK(lv_obj_has_state(find(&display.root, tr("开始翻译  >", "Start translation  >"))->parent,
                           LV_STATE_DISABLED));
    content.translation_mode = MYBOT_TRANSLATION_LISTEN;
    content.input_direction = MYBOT_TRANSLATION_DIRECTION_NONE;
    ++content.selection_revision;
    update(display, content);
    CHECK(find(&display.root, tr("中文", "Chinese")) && find(&display.root, tr("英语", "English")));
    CHECK(!lv_obj_has_state(find(&display.root, tr("开始翻译  >", "Start translation  >"))->parent,
                            LV_STATE_DISABLED));
    click(find(&display.root, tr("开始翻译  >", "Start translation  >")));
    content.screen = MYBOT_LCD_SCREEN_IN_CONVERSATION;
    update(display, content);
    CHECK(find(&display.root, tr("媒体翻译", "Media translation")));
    CHECK(!find(&display.root, tr("面对面翻译", "Face-to-face translation")));
    click(find(&display.root, tr("更多 ...", "More ...")));
    click(find(&display.root, tr("结束这次翻译", "End this translation")));
    click(find(&display.root, tr("结束翻译", "End translation")));
    content.screen = MYBOT_LCD_SCREEN_TRANSLATION_STOPPING;
    update(display, content);
    content.screen = MYBOT_LCD_SCREEN_READY;
    update(display, content);
    CHECK(find(&display.root, tr("翻译已结束", "Translation ended")));
    click(find(&display.root, tr("< 返回", "< Back")));
    CHECK(find(&display.root, home_action()));
    mybot_lvgl_view_destroy();
    CHECK(!live && !timer && display.root.children.empty());
    std::puts(
        "PASS face UI: mode/direction readback, turn selection, pause, per-direction captions "
        "and history, failures and unchanged media controls");
}
static void test_meeting_flow() {
    lv_display_t display;
    display.width = display.root.width = 480;
    display.root.height = 480;
    commands.clear();
    events.clear();
    captions.clear();
    ++caption_generation;
    CHECK(mybot_lvgl_view_create(&display) == 0);
    mybot_lcd_content_t content{};
    content.screen = MYBOT_LCD_SCREEN_READY;
    content.volume = 40;
    content.caption_generation = caption_generation;
    content.usb_audio_available = true;
    std::strcpy(content.source_language, "zh-CN");
    std::strcpy(content.target_language, "en-US");
    update(display, content);
    click(find(&display.root, home_action()));
    CHECK(find(&display.root, tr("双向会议翻译", "Two-way meeting audio")));
    accept_ui_commands = false;
    click(find(&display.root, tr("会议互译", "Meeting")));
    CHECK(commands.empty());
    CHECK(find(&display.root, tr("选择翻译方式", "Choose a translation scene")));
    accept_ui_commands = true;
    click(find(&display.root, tr("会议互译", "Meeting")));
    CHECK(commands.size() == 1 && commands.back().type == MYBOT_UI_SET_MODE &&
          commands.back().value == MYBOT_TRANSLATION_LISTEN_SPEAK &&
          commands.back().audio_source == MYBOT_AUDIO_SOURCE_USB);
    CHECK(!commands.back().source_language[0] && !commands.back().target_language[0]);
    CHECK(lv_obj_has_state(find(&display.root, tr("开始翻译  >", "Start translation  >"))->parent,
                           LV_STATE_DISABLED));
    ++content.volume;
    ++content.selection_revision;
    content.translation_mode = MYBOT_TRANSLATION_LISTEN_SPEAK;
    update(display, content); // The mode alone cannot acknowledge a change to the USB source.
    ticks += 2000;
    timer->callback(timer);
    click(find(&display.root, tr("开始翻译  >", "Start translation  >")));
    CHECK(events.empty());
    content.audio_source = MYBOT_AUDIO_SOURCE_USB;
    std::strcpy(content.source_language, "zh-CN");
    std::strcpy(content.target_language, "en-US");
    ++content.selection_revision;
    update(display, content);
    CHECK(find(&display.root,
               tr("电脑选择设备输入与输出，双方可同时发言",
                  "Select this device for computer input and output. Both sides can speak.")));
    CHECK(find(&display.root, tr("中文", "Chinese"))->parent->x <
          find(&display.root, tr("英语", "English"))->parent->x);
    const size_t available_commands = commands.size();
    std::strcpy(content.source_language, "ja-JP");
    std::strcpy(content.target_language, "ko-KR");
    content.usb_audio_available = false;
    update(display, content);
    CHECK(find(&display.root, tr("USB 不可用", "USB unavailable")));
    CHECK(find(&display.root, tr("日语", "Japanese")) && find(&display.root, tr("韩语", "Korean")));
    CHECK(!find(&display.root, tr("中文", "Chinese")) &&
          !find(&display.root, tr("英语", "English")));
    CHECK(lv_obj_has_state(find(&display.root, tr("日语", "Japanese"))->parent, LV_STATE_DISABLED));
    CHECK(lv_obj_has_state(find(&display.root, tr("韩语", "Korean"))->parent, LV_STATE_DISABLED));
    CHECK(lv_obj_has_state(find(&display.root, tr("开始翻译  >", "Start translation  >"))->parent,
                           LV_STATE_DISABLED));
    click(find(&display.root, tr("日语", "Japanese")));
    click(find(&display.root, tr("开始翻译  >", "Start translation  >")));
    CHECK(commands.size() == available_commands && events.empty());
    content.usb_audio_available = true;
    update(display, content);
    CHECK(!find(&display.root, tr("USB 不可用", "USB unavailable")));
    CHECK(
        !lv_obj_has_state(find(&display.root, tr("日语", "Japanese"))->parent, LV_STATE_DISABLED));
    CHECK(!lv_obj_has_state(find(&display.root, tr("开始翻译  >", "Start translation  >"))->parent,
                            LV_STATE_DISABLED));
    CHECK(commands.size() == available_commands);
    std::strcpy(content.source_language, "zh-CN");
    std::strcpy(content.target_language, "en-US");
    update(display, content);
    click(find(&display.root, tr("中文", "Chinese")));
    content.usb_audio_available = false;
    update(display, content);
    CHECK(lv_obj_has_state(find(&display.root, tr("日语", "Japanese"))->parent, LV_STATE_DISABLED));
    click(find(&display.root, tr("日语", "Japanese")));
    CHECK(commands.size() == available_commands);
    click(find(&display.root, tr("< 返回", "< Back")));
    CHECK(find(&display.root, tr("USB 不可用", "USB unavailable")));
    CHECK(find(&display.root, tr("中文", "Chinese")) && find(&display.root, tr("英语", "English")));
    content.usb_audio_available = true;
    update(display, content);
    click(find(&display.root, tr("中文", "Chinese")));
    CHECK(find(&display.root, tr("我的语言", "My language")));
    click(find(&display.root, tr("日语", "Japanese")));
    CHECK(commands.back().type == MYBOT_UI_SET_LANGUAGES &&
          std::strcmp(commands.back().source_language, "ja-JP") == 0 &&
          std::strcmp(commands.back().target_language, "en-US") == 0);
    const size_t pending_languages = commands.size();
    CHECK(lv_obj_has_state(find(&display.root, tr("英语", "English"))->parent, LV_STATE_DISABLED));
    click(find(&display.root, tr("英语", "English")));
    CHECK(commands.size() == pending_languages);
    click(find(&display.root, tr("< 返回", "< Back")));
    click(find(&display.root, tr("媒体翻译", "Listen")));
    CHECK(commands.size() == pending_languages); // Scene changes also wait for the language pair.
    std::strcpy(content.source_language, "ja-JP");
    update(display, content);
    click(find(&display.root, tr("会议互译", "Meeting")));
    CHECK(commands.size() == pending_languages); // Re-entering preserves the accepted pair.
    click(find(&display.root, tr("日语", "Japanese")));
    click(find(&display.root, tr("中文", "Chinese")));
    std::strcpy(content.source_language, "zh-CN");
    update(display, content);
    click(find(&display.root, tr("英语", "English")));
    CHECK(find(&display.root, tr("对方语言", "Other language")));
    click(find(&display.root, tr("韩语", "Korean")));
    CHECK(commands.back().type == MYBOT_UI_SET_LANGUAGES &&
          std::strcmp(commands.back().source_language, "zh-CN") == 0 &&
          std::strcmp(commands.back().target_language, "ko-KR") == 0);
    std::strcpy(content.target_language, "ko-KR");
    update(display, content);
    click(find(&display.root, tr("开始翻译  >", "Start translation  >")));
    CHECK(events.size() == 1 && events.back() == MYBOT_KEY_EVENT_CONVERSATION_START);
    std::strcpy(content.error_code, "USB_AUDIO_NOT_READY");
    update(display, content);
    CHECK(find(&display.root, tr("电脑音频未开启", "Computer audio is not ready")));
    CHECK(
        find(&display.root,
             tr("请连接电脑，选择设备输入与输出并播放音频",
                "Connect the computer, select this device as input and output, and play audio.")));
    click(find(&display.root, tr("返回设置", "Back to settings")));
    content.error_code[0] = 0;
    update(display, content);
    click(find(&display.root, tr("开始翻译  >", "Start translation  >")));
    CHECK(events.size() == 2 && events.back() == MYBOT_KEY_EVENT_CONVERSATION_START);
    content.screen = MYBOT_LCD_SCREEN_TRANSLATION_STARTING;
    update(display, content);
    content.screen = MYBOT_LCD_SCREEN_IN_CONVERSATION;
    update(display, content);
    CHECK(find(&display.root, tr("会议互译", "Meeting translation")));
    CHECK(find(&display.root, tr("双方可同时发言", "Both sides can speak")));
    CHECK(find(&display.root, tr("中文  <>  韩语", "Chinese  <>  Korean")));
    CHECK(!find(&display.root, tr("中文", "Chinese")));
    CHECK(!find(&display.root, tr("韩语", "Korean")));
    CHECK(slider(&display.root));
    CHECK(!find(&display.root, tr("媒体翻译", "Media translation")));
    CHECK(!find(&display.root, tr("请选择发言语言", "Choose the speaking language")));
    click(find(&display.root, tr("暂停", "Pause")));
    CHECK(commands.back().type == MYBOT_UI_SET_PAUSED && commands.back().value == 1);
    content.paused = true;
    update(display, content);
    CHECK(find(&display.root, tr("继续", "Resume")));
    click(find(&display.root, tr("继续", "Resume")));
    CHECK(commands.back().type == MYBOT_UI_SET_PAUSED && commands.back().value == 0);
    content.paused = false;
    update(display, content);
    mybot_caption_t caption{};
    caption.sequence = 1;
    caption.direction = MYBOT_TRANSLATION_DIRECTION_LISTEN;
    std::strcpy(caption.source_text, "Computer audio.");
    std::strcpy(caption.target_text, "电脑音频。");
    captions.push_back(caption);
    content.caption_count = content.caption_first_sequence = content.caption_latest_sequence = 1;
    ++content.caption_revision;
    update(display, content);
    CHECK(find(&display.root, caption.source_text) && find(&display.root, caption.target_text));
    CHECK(find(&display.root, tr("韩语  >  中文", "Korean  >  Chinese")));
    click(find(&display.root, caption.target_text));
    CHECK(find(&display.root, tr("韩语  >  中文", "Korean  >  Chinese")));
    click(find(&display.root, tr("返回实时翻译", "Back to live")));
    mybot_caption_t speak{};
    speak.sequence = 2;
    speak.direction = MYBOT_TRANSLATION_DIRECTION_SPEAK;
    std::strcpy(speak.source_text, "我的发言。");
    std::strcpy(speak.target_text, "My speech.");
    captions.push_back(speak);
    content.caption_count = content.caption_latest_sequence = 2;
    ++content.caption_revision;
    update(display, content);
    CHECK(find(&display.root, speak.source_text) && find(&display.root, speak.target_text));
    CHECK(!find(&display.root, caption.target_text));
    CHECK(find(&display.root, tr("中文  >  韩语", "Chinese  >  Korean")));
    CHECK(!find(&display.root, tr("韩语  >  中文", "Korean  >  Chinese")));
    const auto allocations_before_caption = allocations;
    std::strcpy(captions.back().target_text, "My revised speech.");
    ++content.caption_revision;
    update(display, content);
    CHECK(allocations == allocations_before_caption);
    CHECK(find(&display.root, captions.back().target_text));
    click(find(&display.root, captions.back().target_text));
    CHECK(find(&display.root, tr("中文  >  韩语", "Chinese  >  Korean")));
    click(find(&display.root, tr("< 返回", "< Back")));
    CHECK(find(&display.root, tr("韩语  >  中文", "Korean  >  Chinese")));
    CHECK(find(&display.root, tr("中文  >  韩语", "Chinese  >  Korean")));
    click(find(&display.root, tr("返回实时翻译", "Back to live")));
    CHECK(std::none_of(commands.begin(), commands.end(),
                       [](const auto &command) { return command.type == MYBOT_UI_SET_DIRECTION; }));
    click(find(&display.root, tr("更多 ...", "More ...")));
    click(find(&display.root, tr("结束这次翻译", "End this translation")));
    click(find(&display.root, tr("结束翻译", "End translation")));
    CHECK(events.back() == MYBOT_KEY_EVENT_CONVERSATION_STOP);
    content.screen = MYBOT_LCD_SCREEN_TRANSLATION_STOPPING;
    update(display, content);
    content.screen = MYBOT_LCD_SCREEN_READY;
    update(display, content);
    CHECK(find(&display.root, tr("翻译已结束", "Translation ended")));
    click(find(&display.root, tr("再来一次", "Translate again")));
    click(find(&display.root, tr("中文", "Chinese")));
    click(find(&display.root, tr("日语", "Japanese")));
    CHECK(commands.back().type == MYBOT_UI_SET_LANGUAGES &&
          std::strcmp(commands.back().source_language, "ja-JP") == 0 &&
          std::strcmp(commands.back().target_language, "ko-KR") == 0);
    std::strcpy(content.source_language, "ja-JP");
    update(display, content);
    click(find(&display.root, tr("< 返回", "< Back")));
    click(find(&display.root, tr("媒体翻译", "Listen")));
    CHECK(commands.back().type == MYBOT_UI_SET_MODE &&
          commands.back().value == MYBOT_TRANSLATION_LISTEN &&
          commands.back().audio_source == MYBOT_AUDIO_SOURCE_CODEC);
    CHECK(!commands.back().source_language[0] && !commands.back().target_language[0]);
    CHECK(lv_obj_has_state(find(&display.root, tr("开始翻译  >", "Start translation  >"))->parent,
                           LV_STATE_DISABLED));
    content.audio_source = MYBOT_AUDIO_SOURCE_CODEC;
    content.translation_mode = MYBOT_TRANSLATION_LISTEN;
    ++content.selection_revision;
    update(display, content);
    CHECK(find(&display.root, tr("日语", "Japanese")) && find(&display.root, tr("韩语", "Korean")));
    CHECK(!lv_obj_has_state(find(&display.root, tr("开始翻译  >", "Start translation  >"))->parent,
                            LV_STATE_DISABLED));
    click(find(&display.root, tr("< 返回", "< Back")));
    click(find(&display.root, tr("面对面", "In person")));
    CHECK(commands.back().type == MYBOT_UI_SET_MODE &&
          commands.back().value == MYBOT_TRANSLATION_LISTEN_SPEAK &&
          commands.back().audio_source == MYBOT_AUDIO_SOURCE_CODEC);
    CHECK(!commands.back().source_language[0] && !commands.back().target_language[0]);
    content.translation_mode = MYBOT_TRANSLATION_LISTEN_SPEAK;
    ++content.selection_revision;
    update(display, content);
    CHECK(find(&display.root, tr("日语", "Japanese")) && find(&display.root, tr("韩语", "Korean")));
    click(find(&display.root, tr("< 返回", "< Back")));
    click(find(&display.root, tr("会议互译", "Meeting")));
    CHECK(commands.back().type == MYBOT_UI_SET_MODE &&
          commands.back().value == MYBOT_TRANSLATION_LISTEN_SPEAK &&
          commands.back().audio_source == MYBOT_AUDIO_SOURCE_USB);
    CHECK(!commands.back().source_language[0] && !commands.back().target_language[0]);
    content.audio_source = MYBOT_AUDIO_SOURCE_USB;
    ++content.selection_revision;
    update(display, content);
    CHECK(find(&display.root, tr("日语", "Japanese")) && find(&display.root, tr("韩语", "Korean")));
    CHECK(!lv_obj_has_state(find(&display.root, tr("开始翻译  >", "Start translation  >"))->parent,
                            LV_STATE_DISABLED));
    mybot_lvgl_view_destroy();
    CHECK(!live && !timer && display.root.children.empty());
    std::puts(
        "PASS meeting UI: USB capability, acknowledged source selection, language mapping, "
        "pending locks, both caption routes, simultaneous pause and preserved scene languages");
}
static void test_language_swap() {
    lv_display_t display;
    display.width = display.root.width = 480;
    display.root.height = 480;
    commands.clear();
    events.clear();
    captions.clear();
    accept_ui_commands = true;
    CHECK(mybot_lvgl_view_create(&display) == 0);
    mybot_lcd_content_t content{};
    content.screen = MYBOT_LCD_SCREEN_READY;
    content.volume = 40;
    content.usb_audio_available = true;
    std::strcpy(content.source_language, "zh-CN");
    std::strcpy(content.target_language, "en-US");
    update(display, content);
    click(find(&display.root, home_action()));
    click(find(&display.root, tr("会议互译", "Meeting")));
    CHECK(commands.size() == 1 && commands.back().type == MYBOT_UI_SET_MODE);
    const auto swap_enabled = [&](bool allow) {
        auto *icon = find(&display.root, "<>");
        CHECK(icon && icon->parent && (icon->parent->flags & LV_OBJ_FLAG_CLICKABLE));
        CHECK(lv_obj_has_state(icon->parent, LV_STATE_DISABLED) != allow);
    };
    swap_enabled(false);
    click(find(&display.root, "<>"));
    CHECK(commands.size() == 1);
    content.translation_mode = MYBOT_TRANSLATION_LISTEN_SPEAK;
    ++content.selection_revision;
    update(display, content); // The RTC mode does not acknowledge the USB source.
    ticks += 2000;
    timer->callback(timer);
    swap_enabled(false);
    click(find(&display.root, "<>"));
    CHECK(commands.size() == 1);
    content.audio_source = MYBOT_AUDIO_SOURCE_USB;
    ++content.selection_revision;
    update(display, content);
    swap_enabled(true);

    content.usb_audio_available = false;
    update(display, content);
    swap_enabled(false);
    click(find(&display.root, "<>"));
    CHECK(commands.size() == 1);
    content.usb_audio_available = true;
    update(display, content);
    accept_ui_commands = false;
    click(find(&display.root, "<>"));
    CHECK(commands.size() == 1);
    swap_enabled(true); // A rejected mailbox must allow an immediate retry.
    accept_ui_commands = true;
    click(find(&display.root, "<>"));
    CHECK(commands.size() == 2 && commands.back().type == MYBOT_UI_SET_LANGUAGES);
    CHECK(std::strcmp(commands.back().source_language, "en-US") == 0 &&
          std::strcmp(commands.back().target_language, "zh-CN") == 0);
    CHECK(find(&display.root, tr("中文", "Chinese"))->parent->x <
          find(&display.root, tr("英语", "English"))->parent->x);
    swap_enabled(false); // Only a stored language pair can change the display.
    click(find(&display.root, "<>"));
    click(find(&display.root, tr("中文", "Chinese")));
    click(find(&display.root, tr("英语", "English")));
    click(find(&display.root, tr("开始翻译  >", "Start translation  >")));
    CHECK(commands.size() == 2 && events.empty());
    ++content.volume;
    ++content.caption_revision;
    ++content.selection_revision;
    update(display, content);
    ticks += 2000;
    timer->callback(timer);
    swap_enabled(false); // Neither unrelated snapshots nor the timer acknowledge languages.
    click(find(&display.root, "<>"));
    click(find(&display.root, tr("开始翻译  >", "Start translation  >")));
    CHECK(commands.size() == 2 && events.empty());
    click(find(&display.root, tr("< 返回", "< Back")));
    click(find(&display.root, tr("媒体翻译", "Listen")));
    click(find(&display.root, tr("面对面", "In person")));
    click(find(&display.root, tr("会议互译", "Meeting")));
    CHECK(commands.size() == 2 && events.empty());
    CHECK(find(&display.root, tr("选择翻译方式", "Choose a translation scene")));

    std::strcpy(content.source_language, "en-US");
    std::strcpy(content.target_language, "zh-CN");
    update(display, content);
    click(find(&display.root, tr("会议互译", "Meeting")));
    CHECK(commands.size() == 2);
    swap_enabled(true);
    CHECK(find(&display.root, tr("英语", "English"))->parent->x <
          find(&display.root, tr("中文", "Chinese"))->parent->x);
    click(find(&display.root, "<>"));
    CHECK(commands.size() == 3 && commands.back().type == MYBOT_UI_SET_LANGUAGES);
    CHECK(std::strcmp(commands.back().source_language, "zh-CN") == 0 &&
          std::strcmp(commands.back().target_language, "en-US") == 0);
    std::strcpy(content.source_language, "zh-CN");
    std::strcpy(content.target_language, "en-US");
    update(display, content);
    swap_enabled(true);

    click(find(&display.root, "<>"));
    CHECK(commands.size() == 4);
    content.screen = MYBOT_LCD_SCREEN_TRANSLATION_ERROR;
    std::strcpy(content.error_code, "STORAGE_ERROR");
    update(display, content);
    CHECK(find(&display.root, "STORAGE_ERROR"));
    click(find(&display.root, tr("返回设置", "Back to settings")));
    swap_enabled(true); // A failed save releases the guard and preserves the accepted pair.
    CHECK(find(&display.root, tr("中文", "Chinese"))->parent->x <
          find(&display.root, tr("英语", "English"))->parent->x);
    content.error_code[0] = '\0';
    update(display, content);
    click(find(&display.root, "<>"));
    CHECK(commands.size() == 5);
    CHECK(std::strcmp(commands.back().source_language, "en-US") == 0 &&
          std::strcmp(commands.back().target_language, "zh-CN") == 0);
    std::strcpy(content.source_language, "en-US");
    std::strcpy(content.target_language, "zh-CN");
    update(display, content);
    swap_enabled(true);

    const char *codec_scenes[] = {tr("面对面", "In person"), tr("媒体翻译", "Listen")};
    for (unsigned scene = 0; scene < 2; ++scene) {
        click(find(&display.root, tr("< 返回", "< Back")));
        const auto before_mode = commands.size();
        click(find(&display.root, codec_scenes[scene]));
        CHECK(commands.size() == before_mode + 1 && commands.back().type == MYBOT_UI_SET_MODE);
        swap_enabled(false);
        content.usb_audio_available = false;
        ++content.volume;
        update(display, content);
        ticks += 2000;
        timer->callback(timer);
        click(find(&display.root, "<>"));
        CHECK(commands.size() == before_mode + 1);
        swap_enabled(false); // Codec scenes also wait for their mode/source acknowledgement.
        content.audio_source = MYBOT_AUDIO_SOURCE_CODEC;
        content.translation_mode =
            scene == 0 ? MYBOT_TRANSLATION_LISTEN_SPEAK : MYBOT_TRANSLATION_LISTEN;
        ++content.selection_revision;
        update(display, content);
        swap_enabled(true); // Neither codec scene requires USB capability.
        const std::string source = content.source_language;
        const std::string target = content.target_language;
        click(find(&display.root, "<>"));
        const auto after_swap = commands.size();
        CHECK(after_swap == before_mode + 2 && commands.back().type == MYBOT_UI_SET_LANGUAGES);
        CHECK(commands.back().source_language == target &&
              commands.back().target_language == source);
        swap_enabled(false);
        click(find(&display.root, "<>"));
        click(find(&display.root, tr("英语", "English")));
        click(find(&display.root, tr("开始翻译  >", "Start translation  >")));
        CHECK(commands.size() == after_swap && events.empty());
        ++content.volume;
        ++content.caption_revision;
        update(display, content);
        ticks += 2000;
        timer->callback(timer);
        swap_enabled(false);
        std::snprintf(content.source_language, sizeof(content.source_language), "%s",
                      target.c_str());
        std::snprintf(content.target_language, sizeof(content.target_language), "%s",
                      source.c_str());
        update(display, content);
        swap_enabled(true);

        // A manual language edit and a swap share the same pending guard.
        click(find(&display.root, scene == 0 ? tr("中文", "Chinese") : tr("英语", "English")));
        click(find(&display.root, scene == 0 ? tr("日语", "Japanese") : tr("韩语", "Korean")));
        CHECK(commands.size() == after_swap + 1 && commands.back().type == MYBOT_UI_SET_LANGUAGES);
        const char *edited_source = scene == 0 ? "ja-JP" : "ko-KR";
        CHECK(std::strcmp(commands.back().source_language, edited_source) == 0 &&
              commands.back().target_language == source);
        swap_enabled(false);
        click(find(&display.root, "<>"));
        click(find(&display.root, tr("开始翻译  >", "Start translation  >")));
        CHECK(commands.size() == after_swap + 1 && events.empty());
        std::snprintf(content.source_language, sizeof(content.source_language), "%s",
                      edited_source);
        update(display, content);
        swap_enabled(true);
    }
    CHECK(events.empty());
    mybot_lvgl_view_destroy();
    CHECK(!live && !timer && display.root.children.empty());
    std::puts("PASS language swap: all scenes, atomic pair, source readiness, mailbox rejection, "
              "acknowledgement locks, failed save and shared language-edit guard");
}
static void test_start_recovery_flow() {
    lv_display_t display;
    display.width = display.root.width = 480;
    display.root.height = 480;
    commands.clear();
    events.clear();
    captions.clear();
    accept_ui_commands = true;
    CHECK(mybot_lvgl_view_create(&display) == 0);
    mybot_lcd_content_t content{};
    content.screen = MYBOT_LCD_SCREEN_START_UNKNOWN;
    content.volume = 37;
    content.selection_revision = 17;
    content.translation_mode = MYBOT_TRANSLATION_LISTEN_SPEAK;
    content.audio_source = MYBOT_AUDIO_SOURCE_USB;
    content.usb_audio_available = true;
    std::strcpy(content.source_language, "zh-CN");
    std::strcpy(content.target_language, "en-US");
    std::strcpy(content.error_code, "START_UNKNOWN");
    update(display, content);
    const char *entry = tr("控制台已结束，恢复设备", "Console sessions ended");
    const char *confirm = tr("确认恢复", "Confirm recovery");
    const char *back = tr("返回", "Back");
    const char *pending = tr("正在恢复...", "Recovering...");
    CHECK(find(&display.root, entry));
    CHECK(!find(&display.root, tr("请稍候", "Please wait")));
    CHECK(!find(&display.root, tr("重试结束", "Retry stop")));
    CHECK(!find(&display.root, home_action()));
    click(find(&display.root, entry));
    CHECK(find(
        &display.root,
        tr("请先在网页控制台结束这台设备的全部会话，再确认恢复设备。",
           "End every session for this device in the web console before confirming recovery.")));
    CHECK(commands.empty() && events.empty());
    click(find(&display.root, back));
    CHECK(find(&display.root, entry) && commands.empty() && events.empty());
    click(find(&display.root, entry));
    accept_ui_commands = false;
    click(find(&display.root, confirm));
    CHECK(commands.empty() && events.empty());
    CHECK(find(&display.root,
               tr("暂时无法恢复，请稍后重试", "Could not submit recovery. Please retry.")));
    CHECK(!lv_obj_has_state(find(&display.root, confirm)->parent, LV_STATE_DISABLED));
    accept_ui_commands = true;
    click(find(&display.root, confirm));
    CHECK(commands.size() == 1 && commands.back().type == MYBOT_UI_CONFIRM_START_RECOVERY &&
          commands.back().value == 1);
    CHECK(lv_obj_has_state(find(&display.root, pending)->parent, LV_STATE_DISABLED));
    CHECK(lv_obj_has_state(find(&display.root, back)->parent, LV_STATE_DISABLED));
    click(find(&display.root, pending));
    ticks += 5000;
    timer->callback(timer);
    ++content.volume;
    update(display,
           content); // Neither the retry timer nor an unrelated snapshot acknowledges recovery.
    CHECK(lv_obj_has_state(find(&display.root, pending)->parent, LV_STATE_DISABLED));
    click(find(&display.root, pending));
    click(find(&display.root, back));
    CHECK(commands.size() == 1 && events.empty());
    ++content.selection_revision;
    update(display, content); // A consumed rejection can retain the same unknown state and error.
    CHECK(find(&display.root, tr("恢复未完成，请重试或重启设备",
                                 "Recovery incomplete. Retry or restart the device.")));
    CHECK(!lv_obj_has_state(find(&display.root, confirm)->parent, LV_STATE_DISABLED));
    click(find(&display.root, confirm));
    CHECK(commands.size() == 2);
    ++content.selection_revision;
    update(display, content); // Repeated same-code rejection is still a real acknowledgement.
    CHECK(find(&display.root, confirm));
    click(find(&display.root, confirm));
    CHECK(commands.size() == 3 && events.empty());
    content.screen = MYBOT_LCD_SCREEN_READY;
    content.error_code[0] = 0;
    ++content.selection_revision;
    update(display, content);
    CHECK(find(&display.root, home_action()));
    CHECK(find(&display.root, tr("输出音量 38%", "Output volume 38%")));
    CHECK(!find(&display.root, tr("翻译已结束", "Translation ended")));
    CHECK(events.empty()); // Recovery never resubmits start.
    content.screen = MYBOT_LCD_SCREEN_START_UNKNOWN;
    std::strcpy(content.error_code, "START_UNKNOWN");
    update(display, content);
    click(find(&display.root, entry));
    click(find(&display.root, confirm));
    CHECK(commands.size() == 4);
    content.screen = MYBOT_LCD_SCREEN_TRANSLATION_ERROR;
    std::strcpy(content.error_code, "STORAGE_ERROR");
    ++content.selection_revision;
    update(display, content);
    CHECK(find(&display.root, tr("恢复失败", "Recovery failed")));
    CHECK(find(&display.root, "STORAGE_ERROR"));
    CHECK(find(&display.root, tr("请重启后重试", "Restart the device to retry")));
    CHECK(lv_obj_has_state(find(&display.root, confirm)->parent, LV_STATE_DISABLED));
    click(find(&display.root, confirm));
    CHECK(commands.size() == 4 && events.empty());
    click(find(&display.root, back));
    CHECK(find(&display.root, "STORAGE_ERROR") && !find(&display.root, entry));
    content.screen = MYBOT_LCD_SCREEN_START_UNKNOWN; // Reboot reloads the retained unknown record.
    std::strcpy(content.error_code, "START_UNKNOWN");
    update(display, content);
    click(find(&display.root, entry));
    CHECK(commands.size() == 4);
    click(find(&display.root, confirm));
    content.screen = MYBOT_LCD_SCREEN_PAIRING; // Revoked authentication returns through pairing.
    content.error_code[0] = 0;
    ++content.selection_revision;
    update(display, content);
    CHECK(find(&display.root, tr("等待配对", "Pairing")));
    CHECK(!find(&display.root, confirm) && !find(&display.root, home_action()));
    CHECK(commands.size() == 5 && events.empty());
    mybot_lvgl_view_destroy();
    CHECK(!live && !timer && display.root.children.empty());
    std::puts("PASS unknown-start recovery UI: explicit confirmation, real acknowledgements, "
              "rejections, retained storage failures and no automatic start");
}
int main() {
    lv_display_t display;
    display.root.width = 480;
    display.root.height = 480;
    CHECK(mybot_lvgl_view_create(nullptr) < 0);
    display.width = 800;
    display.height = 480;
    CHECK(mybot_lvgl_view_create(&display) < 0);
    display.width = 480;
    display.height = 320;
    CHECK(mybot_lvgl_view_create(&display) < 0);
    display.width = 480;
    display.height = 480;
    CHECK(mybot_lvgl_view_create(&display) == 0);
    const unsigned initial_allocations = allocations;
    CHECK(mybot_lvgl_view_create(&display) < 0);
    CHECK(allocations == initial_allocations);
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
    content.caption_generation = caption_generation;
    content.screen = MYBOT_LCD_SCREEN_READY;
    wifi_connected = true;
    content.volume = 60;
    std::strcpy(content.source_language, "zh-CN");
    std::strcpy(content.target_language, "en-US");
    update(display, content);
    CHECK(find(&display.root, tr("Wi-Fi 已连接", "Wi-Fi connected")));
    CHECK(!find(&display.root, tr("电量 --", "Battery --")));
    const auto *start_label = find(&display.root, home_action());
    CHECK(start_label && start_label->text.find('\n') == std::string::npos);
    CHECK(start_label->height == 40);
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
    update(display, content);
    CHECK(find(&display.root, tr("输出音量 37%", "Output volume 37%")));
    click(find(&display.root, home_action()));
    bounds(&display.root);
    // An unavailable USB subsystem cannot enter a synthetic meeting settings flow.
    const auto command_count = commands.size();
    CHECK(lv_obj_has_state(find(&display.root, tr("会议互译", "Meeting"))->parent,
                           LV_STATE_DISABLED));
    CHECK(find(&display.root, tr("USB 不可用", "USB unavailable")));
    click(find(&display.root, tr("会议互译", "Meeting")));
    CHECK(commands.size() == command_count && events.empty());
    CHECK(find(&display.root, tr("选择翻译方式", "Choose a translation scene")));
    CHECK(!find(&display.root, tr("我的语言", "My language")));
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
    update(display, content); // Unrelated readback cannot acknowledge the language pair.
    CHECK(lv_obj_has_state(find(&display.root, tr("英语", "English"))->parent, LV_STATE_DISABLED));
    click(find(&display.root, tr("英语", "English")));
    click(find(&display.root, tr("开始翻译  >", "Start translation  >")));
    CHECK(!find(&display.root, tr("韩语", "Korean")));
    CHECK(commands.size() == language_commands && events.empty());
    std::strcpy(content.source_language, "id-ID");
    update(display, content);
    CHECK(!lv_obj_has_state(find(&display.root, tr("英语", "English"))->parent, LV_STATE_DISABLED));
    click(find(&display.root, tr("英语", "English")));
    click(find(&display.root, tr("韩语", "Korean")));
    CHECK(commands.size() == language_commands + 1);
    CHECK(commands.back().type == MYBOT_UI_SET_LANGUAGES);
    CHECK(std::strcmp(commands.back().source_language, "id-ID") == 0);
    CHECK(std::strcmp(commands.back().target_language, "ko-KR") == 0);
    std::strcpy(content.target_language, "ko-KR");
    update(display, content);
    click(find(&display.root, tr("开始翻译  >", "Start translation  >")));
    CHECK(events.size() == 1 && events.back() == MYBOT_KEY_EVENT_CONVERSATION_START);
    CHECK(!find(&display.root, tr("演示字幕 · 固定示例", "Demo captions · Fixed example")));
    content.screen = MYBOT_LCD_SCREEN_TRANSLATION_STARTING;
    update(display, content);
    click(find(&display.root, tr("取消连接", "Cancel connection")));
    CHECK(events.back() == MYBOT_KEY_EVENT_CONVERSATION_STOP);
    CHECK(!find(&display.root, tr("翻译已结束", "Translation ended")));
    content.screen = MYBOT_LCD_SCREEN_IN_CONVERSATION;
    update(display, content);
    CHECK(!find(&display.root, tr("演示字幕 · 固定示例", "Demo captions · Fixed example")));
    click(find(&display.root, tr("重试结束", "Retry stop")));
    CHECK(events.back() == MYBOT_KEY_EVENT_CONVERSATION_STOP);
    content.screen = MYBOT_LCD_SCREEN_START_UNKNOWN;
    wifi_connected = false;
    update(display, content);
    CHECK(find(&display.root, tr("Wi-Fi 未连接", "Wi-Fi offline")));
    CHECK(find(&display.root, tr("启动结果未知", "Start outcome unknown")));
    CHECK(!find(&display.root, tr("开始翻译  >", "Start translation  >")));
    const auto locked_events = events.size();
    click(find(&display.root, tr("控制台已结束，恢复设备", "Console sessions ended")));
    click(find(&display.root, tr("返回", "Back")));
    CHECK(events.size() == locked_events);
    content.screen = MYBOT_LCD_SCREEN_CLEANUP_PENDING;
    std::strcpy(content.error_code, "cloud_stop_failed");
    update(display, content);
    CHECK(find(&display.root, "cloud_stop_failed"));
    CHECK(find(&display.root, tr("结束尚未完成", "Cleanup pending")));
    click(find(&display.root, tr("重试结束", "Retry stop")));
    CHECK(events.size() == locked_events + 1 && events.back() == MYBOT_KEY_EVENT_CONVERSATION_STOP);
    content.screen = MYBOT_LCD_SCREEN_TRANSLATION_STOPPING;
    update(display, content);
    CHECK(find(&display.root, tr("正在结束翻译", "Ending translation")));
    CHECK(!find(&display.root, tr("翻译已结束", "Translation ended")));
    content.screen = MYBOT_LCD_SCREEN_READY;
    wifi_connected = true;
    std::strcpy(content.error_code, "CLEANUP_AUTH_REJECTED"); // Only READY confirms cleanup.
    update(display, content);
    CHECK(find(&display.root, tr("开始翻译  >", "Start translation  >")));
    CHECK(!find(&display.root, tr("翻译已结束", "Translation ended"))); // Cancel returns to setup.
    CHECK(!find(&display.root, "CLEANUP_AUTH_REJECTED"));
    content.error_code[0] = 0;
    update(display, content);
    click(find(&display.root, tr("开始翻译  >", "Start translation  >")));
    content.screen = MYBOT_LCD_SCREEN_IN_CONVERSATION;
    update(display, content);
    CHECK(find(&display.root, tr("等待字幕", "Waiting for captions")));
    bounds(&display.root);
    content.caption_generation = 0; // Subtitle allocation failure must leave audio UI usable.
    update(display, content);
    CHECK(find(&display.root, tr("字幕暂不可用", "Captions unavailable")));
    CHECK(!find(&display.root, tr("等待字幕", "Waiting for captions")));
    CHECK(find(&display.root, tr("暂停", "Pause")));
    content.caption_generation = caption_generation;
    update(display, content);
    CHECK(find(&display.root, tr("等待字幕", "Waiting for captions")));
    content.caption_generation = caption_generation;
    content.caption_revision = 1;
    content.caption_count = 1;
    content.caption_first_sequence = content.caption_latest_sequence = 1;
    mybot_caption_t first{};
    first.sequence = 1;
    std::strcpy(first.source_text, "真实原文，不是演示。");
    std::strcpy(first.target_text, "This is a real streamed translation.");
    captions.push_back(first);
    const unsigned live_allocations = allocations;
    update(display, content);
    CHECK(allocations == live_allocations); // Subtitle updates preserve all widgets.
    auto *stream_label = find(&display.root, first.target_text);
    CHECK(stream_label && find(&display.root, first.source_text));
    CHECK(stream_label->long_mode == LV_LABEL_LONG_DOT);
    CHECK(find(&display.root, first.source_text)->long_mode == LV_LABEL_LONG_DOT);
    std::strcpy(captions[0].target_text, "A short revision.");
    ++content.caption_revision;
    update(display, content);
    CHECK(find(&display.root, "A short revision.") == stream_label);
    captions_busy = true;
    std::strcpy(captions[0].target_text, "Copy retried without presenter changes.");
    ++content.caption_revision;
    update(display, content);
    CHECK(find(&display.root, "A short revision.") == stream_label);
    captions_busy = false;
    timer->callback(timer);
    CHECK(find(&display.root, "Copy retried without presenter changes.") == stream_label);
    std::strcpy(captions[0].target_text, "A short revision.");
    captions[0].target_truncated = true;
    ++content.caption_revision;
    update(display, content);
    CHECK(find(&display.root, tr("文本过长，已截断", "Long text truncated")));
    captions[0].target_truncated = false;
    click(find(&display.root, tr("暂停", "Pause")));
    CHECK(commands.back().type == MYBOT_UI_SET_PAUSED && commands.back().value == 1);
    CHECK(!find(&display.root,
                tr("继续", "Resume"))); // Only SDK acknowledgement changes pause state.
    const size_t pending_pause_commands = commands.size();
    ++content.caption_revision;
    const unsigned pending_allocations = allocations;
    update(display, content);
    CHECK(allocations == pending_allocations);
    click(find(&display.root, tr("暂停", "Pause")));
    CHECK(commands.size() == pending_pause_commands); // Guard repeated taps before acknowledgement.
    ticks += 2000;
    timer->callback(timer); // A dropped mailbox message must permit retry.
    click(find(&display.root, tr("暂停", "Pause")));
    CHECK(commands.size() == pending_pause_commands + 1);
    update(display, content); // An unchanged readback also releases the local guard.
    click(find(&display.root, tr("暂停", "Pause")));
    CHECK(commands.size() == pending_pause_commands + 2);
    content.paused = true;
    update(display, content);
    CHECK(find(&display.root, tr("继续", "Resume")));
    click(find(&display.root, "A short revision."));
    CHECK(find(&display.root, tr("完整字幕", "Full caption")));
    bounds(&display.root);
    click(find(&display.root, tr("返回实时翻译", "Back to live")));
    click(find(&display.root, tr("更多 ...", "More ...")));
    click(find(&display.root, tr("本次字幕与全文  >", "Captions and full text  >")));
    CHECK(find(&display.root, tr("本次字幕历史", "Caption history")));
    for (uint32_t i = 2; i <= 6; ++i) {
        mybot_caption_t record{};
        record.sequence = i;
        std::snprintf(record.source_text, sizeof(record.source_text), "原文 %u", i);
        std::snprintf(record.target_text, sizeof(record.target_text), "Translation %u", i);
        captions.push_back(record);
    }
    content.caption_count = content.caption_latest_sequence = 6;
    ++content.caption_revision;
    const unsigned history_allocations = allocations;
    update(display, content);
    CHECK(allocations == history_allocations);
    CHECK(find(&display.root, "Translation 4") && !find(&display.root, "Translation 5"));
    captions_busy = true;
    click(find(&display.root, tr("下一页 >", "Next >")));
    CHECK(!find(&display.root, "A short revision.") && !find(&display.root, "Translation 5"));
    captions_busy = false;
    const unsigned retry_allocations = allocations;
    timer->callback(timer);
    CHECK(allocations == retry_allocations);
    CHECK(find(&display.root, "Translation 5") && find(&display.root, "Translation 6"));
    click(find(&display.root, "Translation 5"));
    CHECK(find(&display.root, "Translation 5"));
    auto *full_label = find(&display.root, "Translation 5");
    CHECK((full_label->parent->flags & LV_OBJ_FLAG_SCROLLABLE) != 0);
    CHECK((full_label->parent->flags & LV_OBJ_FLAG_CLICKABLE) != 0);
    mybot_caption_t seventh{};
    seventh.sequence = 7;
    std::strcpy(seventh.source_text, "暂停后的尾稿");
    std::strcpy(seventh.target_text, "Tail caption while paused");
    captions.push_back(seventh);
    content.caption_count = content.caption_latest_sequence = 7;
    ++content.caption_revision;
    const unsigned full_allocations = allocations;
    update(display, content);
    CHECK(allocations == full_allocations && find(&display.root, "Translation 5") == full_label);
    CHECK(!find(&display.root, seventh.target_text)); // Browsing never follows the latest record.
    click(find(&display.root, tr("< 返回", "< Back")));
    CHECK(find(&display.root, "Translation 5") && find(&display.root, seventh.target_text));
    CHECK(!find(&display.root, "A short revision.")); // Back preserves the current history page.
    click(find(&display.root, tr("< 上一页", "< Previous")));
    CHECK(find(&display.root, "A short revision.") && !find(&display.root, "Translation 5"));
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
    update(display, content);
    CHECK(find(&display.root, tr("结束尚未完成", "Cleanup pending")));
    CHECK(!find(&display.root, tr("无法开始翻译", "Could not start translation")));
    CHECK(!find(&display.root, tr("翻译已结束", "Translation ended")));
    click(find(&display.root, tr("重试结束", "Retry stop")));
    CHECK(events.size() == before_end + 2 && events.back() == MYBOT_KEY_EVENT_CONVERSATION_STOP);
    content.screen = MYBOT_LCD_SCREEN_TRANSLATION_STOPPING;
    update(display, content);
    CHECK(find(&display.root, tr("正在结束翻译", "Ending translation")));
    CHECK(!find(&display.root, tr("翻译已结束", "Translation ended")));
    content.screen = MYBOT_LCD_SCREEN_READY;
    update(display, content);
    CHECK(find(&display.root, tr("翻译已结束", "Translation ended")));
    CHECK(!find(&display.root, "CLEANUP_PENDING"));
    update(display, content); // A repeated stale hint must not replace completion.
    CHECK(find(&display.root, tr("翻译已结束", "Translation ended")));
    click(find(&display.root, tr("查看本次字幕", "View captions")));
    CHECK(find(&display.root, "A short revision."));
    click(find(&display.root, "A short revision."));
    CHECK(find(&display.root, tr("完整字幕", "Full caption")));
    click(find(&display.root, tr("返回结束页", "Back to summary")));
    CHECK(find(&display.root, tr("翻译已结束", "Translation ended")));
    // A normal end without a failed stop still uses the SDK acknowledgement.
    content.error_code[0] = 0;
    update(display, content);
    click(find(&display.root, tr("再来一次", "Translate again")));
    click(find(&display.root, tr("开始翻译  >", "Start translation  >")));
    content.screen = MYBOT_LCD_SCREEN_IN_CONVERSATION;
    update(display, content);
    click(find(&display.root, tr("更多 ...", "More ...")));
    click(find(&display.root, tr("结束这次翻译", "End this translation")));
    click(find(&display.root, tr("结束翻译", "End translation")));
    content.screen = MYBOT_LCD_SCREEN_TRANSLATION_STOPPING;
    update(display, content);
    content.screen = MYBOT_LCD_SCREEN_READY;
    update(display, content);
    CHECK(find(&display.root, tr("翻译已结束", "Translation ended")));
    // A new session generation clears the previous local history immediately.
    ++caption_generation;
    content.caption_generation = caption_generation;
    content.caption_revision = 0;
    content.caption_count = content.caption_first_sequence = content.caption_latest_sequence = 0;
    captions.clear();
    // Changing settings, unlike a regular end, returns to setup after cleanup.
    click(find(&display.root, tr("再来一次", "Translate again")));
    click(find(&display.root, tr("开始翻译  >", "Start translation  >")));
    content.screen = MYBOT_LCD_SCREEN_IN_CONVERSATION;
    update(display, content);
    CHECK(find(&display.root, tr("等待字幕", "Waiting for captions")));
    CHECK(!find(&display.root, "Tail caption while paused"));
    mybot_caption_t new_session{};
    new_session.sequence = 1;
    std::strcpy(new_session.source_text, "新一场字幕");
    std::strcpy(new_session.target_text, "A new session caption");
    captions.push_back(new_session);
    content.caption_revision = 1;
    content.caption_count = content.caption_first_sequence = content.caption_latest_sequence = 1;
    update(display, content);
    CHECK(find(&display.root, new_session.target_text));
    // Resetting only the subtitle generation must erase old text even if reads are busy.
    ++caption_generation;
    content.caption_generation = caption_generation;
    captions_busy = true;
    update(display, content);
    CHECK(!find(&display.root, new_session.target_text));
    CHECK(find(&display.root, tr("等待字幕", "Waiting for captions")));
    captions_busy = false;
    timer->callback(timer);
    CHECK(find(&display.root, new_session.target_text));
    click(find(&display.root, content.paused ? tr("继续", "Resume") : tr("暂停", "Pause")));
    click(find(&display.root, new_session.target_text));
    auto *retained_full_label = find(&display.root, new_session.target_text);
    for (uint32_t sequence = 2; sequence <= 65; ++sequence) {
        mybot_caption_t record{};
        record.sequence = sequence;
        std::snprintf(record.target_text, sizeof(record.target_text), "Retained translation %u",
                      sequence);
        captions.push_back(record);
    }
    content.caption_count = MYBOT_CAPTION_HISTORY_CAPACITY;
    content.caption_first_sequence = 2;
    content.caption_latest_sequence = 65;
    ++content.caption_revision;
    const unsigned eviction_allocations = allocations, eviction_reads = caption_reads;
    update(display, content);
    CHECK(find(&display.root, new_session.target_text) == retained_full_label);
    CHECK(!find(&display.root, "Retained translation 2"));
    CHECK(allocations == eviction_allocations && caption_reads == eviction_reads);
    ticks += 2000;
    timer->callback(timer); // Retain the copied sentence across a local control redraw too.
    CHECK(find(&display.root, new_session.target_text));
    CHECK(caption_reads == eviction_reads);
    click(find(&display.root, tr("< 返回", "< Back")));
    CHECK(find(&display.root, "Retained translation 2"));
    CHECK(!find(&display.root, new_session.target_text));
    // If the selected record disappears before its first read, show no other sentence.
    captions_busy = true;
    click(find(&display.root, "Retained translation 2"));
    mybot_caption_t newest{};
    newest.sequence = 66;
    std::strcpy(newest.target_text, "Retained translation 66");
    captions.push_back(newest);
    content.caption_first_sequence = 3;
    content.caption_latest_sequence = 66;
    ++content.caption_revision;
    update(display, content);
    CHECK(find(&display.root, tr("请返回字幕历史", "Return to caption history")));
    CHECK(!find(&display.root, "Retained translation 3"));
    captions_busy = false;
    click(find(&display.root, tr("< 返回", "< Back")));
    CHECK(find(&display.root, "Retained translation 3"));
    click(find(&display.root, tr("返回实时翻译", "Back to live")));
    CHECK(find(&display.root, "Retained translation 66"));
    click(find(&display.root, tr("更多 ...", "More ...")));
    click(find(&display.root, tr("更改语言与使用方式  >", "Change languages and scene  >")));
    click(find(&display.root, tr("结束并更换", "End and change")));
    content.screen = MYBOT_LCD_SCREEN_TRANSLATION_STOPPING;
    update(display, content);
    content.screen = MYBOT_LCD_SCREEN_READY;
    std::strcpy(content.error_code, "CLEANUP_PENDING");
    update(display, content);
    CHECK(find(&display.root, tr("开始翻译  >", "Start translation  >")));
    CHECK(!find(&display.root, tr("翻译已结束", "Translation ended")));
    for (const char *error : {"MEDIA_NOT_READY", "RTC_NOT_READY", "CONVERSATION_START_FAILED",
                              "TIME_NOT_SYNCED", "STORAGE_ERROR"}) {
        content.screen = MYBOT_LCD_SCREEN_READY;
        std::strcpy(content.error_code, error);
        update(display, content);
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
        update(display, content); // Repeated same-state rejection stays visible.
        CHECK(find(&display.root, error));
    }
    // Updating a READY error screen with a stale cleanup hint must leave no wait screen.
    std::strcpy(content.error_code, "CLEANUP_PENDING");
    update(display, content);
    CHECK(find(&display.root, tr("开始翻译  >", "Start translation  >")));
    CHECK(!find(&display.root, tr("请稍候", "Please wait")));
    CHECK(!find(&display.root, tr("无法开始翻译", "Could not start translation")));
    content.screen = MYBOT_LCD_SCREEN_TRANSLATION_ERROR;
    std::strcpy(content.error_code, "MEDIA_NOT_READY");
    update(display, content);
    CHECK(find(&display.root,
               tr("本地音频未恢复，请重试", "Local audio has not recovered. Please retry.")));
    click(find(&display.root, tr("返回设置", "Back to settings")));
    click(find(&display.root, tr("韩语", "Korean")));
    click(find(&display.root, tr("日语", "Japanese")));
    content.volume = 39;
    update(display, content); // An unchanged old error is not a language result.
    CHECK(lv_obj_has_state(find(&display.root, tr("韩语", "Korean"))->parent, LV_STATE_DISABLED));
    std::strcpy(content.target_language, "ja-JP");
    content.error_code[0] = 0;
    update(display, content);
    CHECK(
        !lv_obj_has_state(find(&display.root, tr("日语", "Japanese"))->parent, LV_STATE_DISABLED));
    content.screen = MYBOT_LCD_SCREEN_READY;
    content.error_code[0] = 0;
    update(display, content);
    click(find(&display.root, home_action()));
    click(find(&display.root, tr("媒体翻译", "Listen")));
    click(find(&display.root, tr("日语", "Japanese")));
    click(find(&display.root, tr("韩语", "Korean")));
    CHECK(lv_obj_has_state(find(&display.root, tr("日语", "Japanese"))->parent, LV_STATE_DISABLED));
    content.screen = MYBOT_LCD_SCREEN_TRANSLATION_ERROR;
    std::strcpy(content.error_code, "STORAGE_ERROR");
    update(display, content); // A failed save must release the language guard too.
    click(find(&display.root, tr("返回设置", "Back to settings")));
    CHECK(
        !lv_obj_has_state(find(&display.root, tr("日语", "Japanese"))->parent, LV_STATE_DISABLED));
    content.screen = MYBOT_LCD_SCREEN_PAIR_CODE;
    std::strcpy(content.pair_code, "654321");
    update(display, content);
    CHECK(find(&display.root, "654321"));
    content.screen = MYBOT_LCD_SCREEN_WIFI_PROVISIONING;
    wifi_connected = false;
    update(display, content);
    CHECK(find(&display.root, "Translation-WiFi"));
    CHECK(find(&display.root, tr("Wi-Fi 未连接", "Wi-Fi offline")));
    timer->callback(timer);
    mybot_lvgl_view_destroy();
    mybot_lvgl_view_destroy();
    CHECK(!live && !timer && display.root.children.empty());
    test_face_flow();
    test_meeting_flow();
    test_language_swap();
    test_start_recovery_flow();
    std::puts("PASS native simulator UI: USB availability, direct language selection, real volume, "
              "pause, streamed captions/history, lifecycle gates and allocation cleanup");
}
