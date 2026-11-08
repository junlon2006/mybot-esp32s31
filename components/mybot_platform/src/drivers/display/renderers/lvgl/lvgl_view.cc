/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Project Contributors */
#include "display/lvgl_view.h"
#include "input/board_keys.h"
#include "network/wifi_control.h"
#include "sdkconfig.h"

#include <mybot/mybot_ui.h>
#include <mybot/mybot_captions.h>

#include <cstdio>
#include <cstring>
#include <ctime>

LV_FONT_DECLARE(mybot_lvgl_font_20);
LV_FONT_DECLARE(mybot_lvgl_font_28);
LV_FONT_DECLARE(mybot_lvgl_font_40);
LV_FONT_DECLARE(mybot_caption_font_20);
LV_FONT_DECLARE(mybot_caption_font_28);

namespace {
constexpr uint32_t kBackground = 0xb9d3ff;
constexpr uint32_t kInk = 0x20334a;
constexpr uint32_t kMuted = 0x52647a;
constexpr uint32_t kWhite = 0xf7fbff;
constexpr uint32_t kTeal = 0x8ee3df;
constexpr uint32_t kPurple = 0xc8b9f3;
constexpr uint32_t kYellow = 0xf5e7ad;
constexpr uint32_t kMint = 0xb9e6c4;
constexpr uint32_t kPink = 0xf3c1d9;
constexpr uint32_t kSoft = 0xdbe7fc;
constexpr uint32_t kWarning = 0x7b334b;
constexpr unsigned kLanguagesPerPage = 6;
constexpr unsigned kCaptionsPerPage = 4;
#if CONFIG_MYBOT_LANGUAGE_ZH_CN
constexpr const char *tr(const char *zh, const char *) {
    return zh;
}
#else
constexpr const char *tr(const char *, const char *en) {
    return en;
}
#endif

enum class Page {
    Home,
    Entry,
    Setup,
    Languages,
    Live,
    Menu,
    Sound,
    Full,
    History,
    EndConfirm,
    ChangeConfirm,
    Complete,
    Service
};
enum class Scene { Media, Face, Meeting };
enum Action : uintptr_t {
    NoAction = 0,
    Home,
    Entry,
    Media,
    Face,
    Meeting,
    Setup,
    Source,
    Target,
    Start,
    Menu,
    Live,
    Sound,
    Full,
    History,
    Pause,
    Speak,
    Listen,
    End,
    ConfirmEnd,
    Change,
    ConfirmChange,
    RetryStop,
    CancelStart,
    CaptionExit,
    HistoryPrevious,
    HistoryNext,
    LanguagePrevious,
    LanguageNext,
    Volume,
    Brightness,
    LanguageBase = 100,
    CaptionBase = 200
};
struct Slider {
    lv_obj_t *object{};
    lv_obj_t *fill{};
    lv_obj_t *knob{};
    int width = 0;
    int value = 0;
    bool brightness = false;
    bool dragging = false;
};
struct View {
    lv_obj_t *root{};
    lv_obj_t *body{};
    lv_obj_t *clock{};
    lv_obj_t *network{};
    lv_obj_t *home_time{};
    lv_obj_t *home_date{};
    lv_obj_t *volume_label{};
    Slider volume_slider;
    Slider brightness_slider;
    lv_timer_t *timer{};
    mybot_caption_t *caption{};
    lv_obj_t *caption_source{};
    lv_obj_t *caption_target{};
    lv_obj_t *caption_notice{};
    lv_obj_t *caption_route{};
    lv_obj_t *history_route[kCaptionsPerPage]{};
    lv_obj_t *history_target[kCaptionsPerPage]{};
    lv_obj_t *history_buttons[kCaptionsPerPage]{};
    lv_obj_t *history_previous{};
    lv_obj_t *history_next{};
    uint32_t history_sequences[kCaptionsPerPage]{};
    uint32_t history_first = 0;
    uint32_t selected_sequence = 0;
    bool captions_dirty = false;
    mybot_lcd_content_t content{};
    Page page = Page::Service;
    Scene scene = Scene::Media;
    bool pick_target = false;
    bool failed = false;
    bool command_pending = false;
    int pending_mode = -1;
    int pending_audio_source = -1;
    int pending_direction = -1;
    uint32_t pending_selection_revision = 0;
    bool start_requested = false;
    uint32_t pending_since = 0;
    bool stop_requested = false;
    bool change_after_stop = false;
    bool cancel_requested = false;
    unsigned language_page = 0;
    char pending_source[33]{};
    char pending_target[33]{};
    int brightness = 60;
    int width = 800;
    char preview_source[33] = "zh-CN";
    char preview_target[33] = "en-US";
};
View s_view;

bool compact() {
    return s_view.width == 480;
}
int margin() {
    return compact() ? 32 : 24;
}
int content_width() {
    return s_view.width - 2 * margin();
}

bool idle() {
    return s_view.content.screen == MYBOT_LCD_SCREEN_READY ||
           s_view.content.screen == MYBOT_LCD_SCREEN_TRANSLATION_ERROR;
}
bool active() {
    return s_view.content.screen == MYBOT_LCD_SCREEN_IN_CONVERSATION;
}
bool languages_pending() {
    return s_view.pending_source[0] != '\0';
}
bool face_session() {
    return s_view.content.translation_mode == MYBOT_TRANSLATION_LISTEN_SPEAK &&
           s_view.content.audio_source == MYBOT_AUDIO_SOURCE_CODEC;
}
bool meeting_session() {
    return s_view.content.audio_source == MYBOT_AUDIO_SOURCE_USB;
}
bool meeting_preview() {
    return s_view.scene == Scene::Meeting && !s_view.content.usb_audio_available;
}
mybot_translation_mode_t scene_mode() {
    return s_view.scene == Scene::Media ? MYBOT_TRANSLATION_LISTEN : MYBOT_TRANSLATION_LISTEN_SPEAK;
}
mybot_audio_source_t scene_audio_source() {
    return s_view.scene == Scene::Meeting ? MYBOT_AUDIO_SOURCE_USB : MYBOT_AUDIO_SOURCE_CODEC;
}
bool mode_ready() {
    return !meeting_preview() && s_view.pending_mode < 0 &&
           s_view.content.translation_mode == scene_mode() &&
           s_view.content.audio_source == scene_audio_source();
}
bool languages_allowed() {
    return meeting_preview() || (mode_ready() && !s_view.command_pending && !languages_pending());
}
uint32_t live_caption_sequence() {
    if (!face_session())
        return s_view.content.caption_latest_sequence;
    if (s_view.content.input_direction == MYBOT_TRANSLATION_DIRECTION_SPEAK)
        return s_view.content.caption_latest_speak_sequence;
    if (s_view.content.input_direction == MYBOT_TRANSLATION_DIRECTION_LISTEN)
        return s_view.content.caption_latest_listen_sequence;
    return 0;
}
bool completed_cleanup_hint() {
    /* READY confirms that the SDK no longer owns pending cleanup. An old cleanup
     * hint must not turn that acknowledgement into a new start failure. */
    const char *error = s_view.content.error_code;
    return s_view.content.screen == MYBOT_LCD_SCREEN_READY &&
           (std::strcmp(error, "CLEANUP_PENDING") == 0 ||
            std::strcmp(error, "CLEANUP_AUTH_REJECTED") == 0);
}
bool idle_error() {
    return idle() && s_view.content.error_code[0] && !completed_cleanup_hint();
}
bool volume_allowed() {
    return s_view.content.volume >= 0 && (idle() || active());
}

void redraw();
void refresh_captions();
void clicked(lv_event_t *event);
void slider_event(lv_event_t *event);
void command(mybot_ui_command_type_t type, int value);

lv_obj_t *panel(lv_obj_t *parent, int x, int y, int width, int height, uint32_t color,
                int radius = 20) {
    if (!parent)
        return nullptr;
    lv_obj_t *object = lv_obj_create(parent);
    if (!object) {
        s_view.failed = true;
        return nullptr;
    }
    lv_obj_remove_style_all(object);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_size(object, width, height);
    lv_obj_set_style_bg_color(object, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(object, radius, 0);
    return object;
}
lv_obj_t *label(lv_obj_t *parent, int x, int y, int width, int height, const char *text,
                const lv_font_t *font = &mybot_lvgl_font_20,
                lv_text_align_t align = LV_TEXT_ALIGN_CENTER, uint32_t color = kInk) {
    if (!parent)
        return nullptr;
    lv_obj_t *object = lv_label_create(parent);
    if (!object) {
        s_view.failed = true;
        return nullptr;
    }
    lv_obj_set_pos(object, x, y);
    lv_obj_set_size(object, width, height);
    lv_obj_set_style_text_font(object, font, 0);
    lv_obj_set_style_text_color(object, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(object, align, 0);
    lv_label_set_long_mode(object, LV_LABEL_LONG_WRAP);
    lv_label_set_text(object, text);
    return object;
}
void enabled(lv_obj_t *object, bool allow) {
    if (!object)
        return;
    if (allow)
        lv_obj_remove_state(object, LV_STATE_DISABLED);
    else
        lv_obj_add_state(object, LV_STATE_DISABLED);
    lv_obj_set_style_opa(object, allow ? LV_OPA_COVER : LV_OPA_30, 0);
}
lv_obj_t *button(int x, int y, int width, int height, const char *text, Action action,
                 uint32_t color = kWhite, bool allow = true, int radius = 18) {
    lv_obj_t *object = panel(s_view.body, x, y, width, height, color, radius);
    if (!object)
        return nullptr;
    lv_obj_add_flag(object, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(object, clicked, LV_EVENT_CLICKED,
                        reinterpret_cast<void *>(static_cast<uintptr_t>(action)));
    const int caption_height = height >= 64 ? 52 : 30;
    label(object, 10, (height - caption_height) / 2, width - 20, caption_height, text);
    enabled(object, allow);
    return object;
}
void heading(const char *title, Action back = NoAction, const char *right = nullptr,
             Action right_action = NoAction) {
    const int side = compact() ? 96 : 124;
    const int inset = margin();
    if (back != NoAction)
        button(inset, 10, side, 44, tr("< 返回", "< Back"), back, kSoft);
    const int left = back != NoAction ? inset + side + 8 : inset;
    const int edge = right ? inset + side + 8 : inset;
    label(s_view.body, left, 17, s_view.width - left - edge, 48, title,
          compact() ? &mybot_lvgl_font_20 : &mybot_lvgl_font_28);
    if (right)
        button(s_view.width - inset - side, 10, side, 44, right, right_action, kSoft);
}
const char *language_name(const char *code) {
    size_t count = 0;
    const mybot_ui_language_t *languages = mybot_ui_languages(&count);
    for (size_t i = 0; i < count; ++i)
        if (std::strcmp(code, languages[i].code) == 0)
            return tr(languages[i].name_zh, languages[i].name_en);
    return code[0] ? code : "--";
}
const char *source_code() {
    return meeting_preview() ? s_view.preview_source : s_view.content.source_language;
}
const char *target_code() {
    return meeting_preview() ? s_view.preview_target : s_view.content.target_language;
}
const char *scene_name() {
    return s_view.scene == Scene::Media  ? tr("媒体翻译", "Media translation")
           : s_view.scene == Scene::Face ? tr("面对面", "In person")
                                         : tr("会议互译", "Meeting");
}
void mark_pending() {
    s_view.command_pending = true;
    s_view.pending_since = lv_tick_get();
}
void update_clock(lv_timer_t *timer) {
    /* Mailbox rejection must never leave a UI button permanently disabled.
     * This only releases a local button guard; SDK state still owns all outcomes. */
    if (timer && s_view.command_pending && lv_tick_get() - s_view.pending_since >= 1500) {
        s_view.command_pending = false;
        redraw();
    }
    refresh_captions();
    const time_t now = std::time(nullptr);
    char clock[16] = "--:--";
    char date[64];
    std::snprintf(date, sizeof(date), "%s", tr("等待时间同步", "Waiting for time sync"));
    if (now >= 1704067200) {
        /* Present Beijing time without changing the process-wide timezone. */
        const time_t beijing = now + 8 * 60 * 60;
        std::tm fields{};
        if (gmtime_r(&beijing, &fields)) {
            std::strftime(clock, sizeof(clock), "%H:%M", &fields);
            std::strftime(date, sizeof(date), "%Y.%m.%d   UTC+8", &fields);
        }
    }
    if (s_view.clock)
        lv_label_set_text(s_view.clock, clock);
    if (s_view.home_time)
        lv_label_set_text(s_view.home_time, clock);
    if (s_view.home_date)
        lv_label_set_text(s_view.home_date, date);
    if (s_view.network)
        lv_label_set_text(s_view.network, mybot_wifi_is_connected()
                                              ? tr("Wi-Fi 已连接", "Wi-Fi connected")
                                              : tr("Wi-Fi 未连接", "Wi-Fi offline"));
}
void set_slider(Slider &slider, int value) {
    if (!slider.object || !slider.fill || !slider.knob)
        return;
    slider.value = value < 0 ? 0 : value > 100 ? 100 : value;
    const int offset = (slider.width - 26) * slider.value / 100;
    lv_obj_set_size(slider.fill, offset, 10);
    lv_obj_set_pos(slider.knob, offset, 5);
}
void update_volume() {
    char text[64];
    if (s_view.content.volume < 0)
        std::snprintf(text, sizeof(text), "%s --", tr("输出音量", "Output volume"));
    else
        std::snprintf(text, sizeof(text), "%s %d%%", tr("输出音量", "Output volume"),
                      s_view.content.volume);
    if (s_view.volume_label)
        lv_label_set_text(s_view.volume_label, text);
    if (!s_view.volume_slider.dragging)
        set_slider(s_view.volume_slider, s_view.content.volume);
    enabled(s_view.volume_slider.object, volume_allowed());
}
void slider(int x, int y, int width, bool brightness) {
    lv_obj_t *card = panel(s_view.body, x, y, width, 86, brightness ? kPink : kMint, 22);
    lv_obj_t *caption = label(card, 14, 8, width - 28, 28,
                              brightness ? tr("亮度 · 演示", "Brightness · Demo") : "");
    if (!brightness)
        s_view.volume_label = caption;
    Slider &control = brightness ? s_view.brightness_slider : s_view.volume_slider;
    control.brightness = brightness;
    control.width = width - 60;
    control.object = panel(card, 30, 38, control.width, 44, brightness ? kPink : kMint, 0);
    panel(control.object, 13, 13, control.width - 26, 10, kWhite, 5);
    control.fill = panel(control.object, 13, 13, 0, 10, kMuted, 5);
    control.knob = panel(control.object, 0, 5, 26, 26, kInk, 13);
    if (!control.object)
        return;
    /* A local pointer control uses the pinned upstream object primitives.
     * PRESS_LOCK keeps a drag within this control until release or cancellation. */
    lv_obj_add_flag(control.object, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(control.object, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(control.object, slider_event, LV_EVENT_ALL, &control);
    set_slider(control, brightness ? s_view.brightness : s_view.content.volume);
}
void slider_event(lv_event_t *event) {
    const auto code = lv_event_get_code(event);
    if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING && code != LV_EVENT_RELEASED &&
        code != LV_EVENT_PRESS_LOST)
        return;
    auto *control = static_cast<Slider *>(lv_event_get_user_data(event));
    if (!control || !control->object || control->object != lv_event_get_current_target_obj(event) ||
        lv_obj_has_state(control->object, LV_STATE_DISABLED))
        return;
    if (code == LV_EVENT_PRESS_LOST) {
        control->dragging = false;
        set_slider(*control, control->brightness ? s_view.brightness : s_view.content.volume);
        return;
    }
    if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING) {
        lv_indev_t *input = lv_indev_active();
        if (!input)
            return;
        if (code == LV_EVENT_PRESSED)
            control->dragging = true;
        if (!control->dragging)
            return;
        lv_point_t point{};
        lv_area_t bounds{};
        lv_indev_get_point(input, &point);
        lv_obj_get_coords(control->object, &bounds);
        const int travel = control->width - 26;
        set_slider(*control, ((point.x - bounds.x1 - 13) * 100 + travel / 2) / travel);
    } else if (code == LV_EVENT_RELEASED && control->dragging) {
        control->dragging = false;
        if (control->brightness)
            s_view.brightness = control->value;
        else if (volume_allowed()) {
            command(MYBOT_UI_SET_VOLUME, control->value);
            update_volume();
        }
    }
}

void utilities(int y) {
    if (compact()) {
        slider(margin(), y > 326 ? 326 : y, content_width(), false);
    } else {
        slider(24, y, 364, false);
        slider(412, y, 364, true);
    }
    update_volume();
}
void home() {
    const int left = compact() ? margin() : 40;
    label(s_view.body, left, compact() ? 12 : 30, compact() ? content_width() : 350, 38,
          tr("你好，今天也有新发现", "Hello, Sunshine!"), &mybot_lvgl_font_28, LV_TEXT_ALIGN_LEFT);
    s_view.home_date = label(s_view.body, left, compact() ? 58 : 82, 360, 30, "",
                             &mybot_lvgl_font_20, LV_TEXT_ALIGN_LEFT, kMuted);
    s_view.home_time = label(s_view.body, left, compact() ? 94 : 122, 340, compact() ? 56 : 70, "",
                             &mybot_lvgl_font_40, LV_TEXT_ALIGN_LEFT);
    if (!compact())
        label(s_view.body, 42, 198, 350, 32, tr("北京时间 / UTC+8", "Beijing time / UTC+8"),
              &mybot_lvgl_font_20, LV_TEXT_ALIGN_LEFT, kMuted);
    auto *orb = compact() ? panel(s_view.body, margin(), 174, content_width(), 96, kTeal, 24)
                          : panel(s_view.body, 454, 28, 236, 236, kTeal, 118);
    if (orb) {
        lv_obj_add_flag(orb, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(orb, clicked, LV_EVENT_CLICKED, reinterpret_cast<void *>(Entry));
        label(orb, 18, compact() ? 28 : 72, compact() ? content_width() - 36 : 200,
              compact() ? 40 : 92,
              compact() ? tr("开始翻译 >", "Start translation >")
                        : tr("开始\n翻译  >", "Let's\ntranslate  >"),
              &mybot_lvgl_font_28);
    }
    if (!compact())
        label(s_view.body, 650, 258, 140, 30, tr("电量 --", "Battery --"));
    utilities(compact() ? 286 : 318);
}
void entry() {
    heading(tr("选择翻译方式", "Choose a translation scene"), Home);
    const char *names[] = {tr("媒体翻译", "Listen"), tr("面对面", "In person"),
                           tr("会议互译", "Meeting")};
    const char *notes[] = {tr("单向听译", "One way"), tr("轮流交流", "Take turns"),
                           s_view.content.usb_audio_available
                               ? tr("双向会议翻译", "Two-way meeting audio")
                               : tr("听译与发言 · 待开放", "Two way · Soon")};
    const Action actions[] = {Media, Face, Meeting};
    const uint32_t colors[] = {kTeal, kPurple, kYellow};
    for (unsigned i = 0; i < 3; ++i) {
        auto *card = button(
            compact() ? margin() : 24 + i * 256, compact() ? 78 + i * 112 : 108,
            compact() ? content_width() : 240, compact() ? 100 : 212, "", actions[i], colors[i],
            s_view.pending_mode < 0 && !s_view.command_pending && !languages_pending());
        label(card, 12, compact() ? 12 : 54, compact() ? content_width() - 24 : 216, 40, names[i],
              &mybot_lvgl_font_28);
        label(card, 12, compact() ? 58 : 116, compact() ? content_width() - 24 : 216,
              compact() ? 30 : 60, notes[i], &mybot_lvgl_font_20);
    }
    if (!compact())
        label(s_view.body, 24, 348, 752, 46,
              tr("请选择适合的翻译方式", "Choose the translation scene that suits you"),
              &mybot_lvgl_font_20, LV_TEXT_ALIGN_CENTER, kMuted);
}
void setup() {
    heading(scene_name(), Entry);
    const int column = compact() ? 184 : 330;
    const int left = compact() ? margin() : 28;
    const int right = s_view.width - left - column;
    label(s_view.body, left, 78, column, 30,
          s_view.scene == Scene::Media ? tr("输入语言", "Input language")
                                       : tr("我的语言", "My language"));
    label(s_view.body, right, 78, column, 30,
          s_view.scene == Scene::Media ? tr("翻译为", "Translate to")
                                       : tr("对方语言", "Other language"));
    button(left, 116, column, 96, language_name(source_code()), Source, kTeal, languages_allowed());
    label(s_view.body, left + column, 144, right - left - column, 40,
          s_view.scene == Scene::Media ? ">" : "<>", &mybot_lvgl_font_28);
    button(right, 116, column, 96, language_name(target_code()), Target, kPurple,
           languages_allowed());
    label(s_view.body, margin(), 236, content_width(), 76,
          s_view.pending_mode >= 0 ? tr("正在切换翻译方式", "Changing translation mode")
          : s_view.scene == Scene::Media
              ? tr("板载麦克风拾音，板载扬声器播放译音",
                   "Board microphone input and speaker translation output")
          : s_view.scene == Scene::Face
              ? tr("连接后选择发言语言，双方轮流交流",
                   "Connect, then select each speaker's language to take turns")
          : !meeting_preview()
              ? tr("电脑选择设备输入与输出，双方可同时发言",
                   "Select this device for computer input and output. Both sides can speak.")
              : tr("界面预览，业务待实现", "UI preview. This scene is not available yet."),
          &mybot_lvgl_font_20, LV_TEXT_ALIGN_CENTER, kMuted);
    button((s_view.width - 364) / 2, 328, 364, 64,
           !meeting_preview() ? tr("开始翻译  >", "Start translation  >")
                              : tr("待开放", "Coming soon"),
           Start, !meeting_preview() ? kTeal : kSoft,
           mode_ready() && idle() && !s_view.command_pending && !languages_pending());
}
void languages() {
    heading(s_view.scene == Scene::Media ? (s_view.pick_target ? tr("翻译为", "Translate to")
                                                               : tr("输入语言", "Input language"))
                                         : (s_view.pick_target ? tr("对方语言", "Other language")
                                                               : tr("我的语言", "My language")),
            Setup);
    size_t count = 0;
    const auto *list = mybot_ui_languages(&count);
    const unsigned begin = s_view.language_page * kLanguagesPerPage;
    const char *other = s_view.pick_target ? source_code() : target_code();
    const char *selected = s_view.pick_target ? target_code() : source_code();
    for (unsigned i = 0; i < kLanguagesPerPage && begin + i < count; ++i) {
        const auto &item = list[begin + i];
        const bool chosen = std::strcmp(item.code, selected) == 0;
        constexpr int gap = 16;
        const int column = (content_width() - gap) / 2;
        button(margin() + (i % 2) * (column + gap), 84 + (i / 2) * 88, column, 72,
               tr(item.name_zh, item.name_en), static_cast<Action>(LanguageBase + begin + i),
               chosen ? kTeal : kWhite, std::strcmp(item.code, other) != 0 && languages_allowed());
    }
    button(margin(), 358, compact() ? 142 : 190, 48, tr("< 上一页", "< Previous"), LanguagePrevious,
           kSoft, s_view.language_page > 0);
    char text[32];
    std::snprintf(text, sizeof(text), "%u / %u", s_view.language_page + 1,
                  static_cast<unsigned>((count + kLanguagesPerPage - 1) / kLanguagesPerPage));
    label(s_view.body, (s_view.width - 120) / 2, 366, 120, 32, text);
    button(s_view.width - margin() - (compact() ? 142 : 190), 358, compact() ? 142 : 190, 48,
           tr("下一页 >", "Next >"), LanguageNext, kSoft, begin + kLanguagesPerPage < count);
}
void format_route(char *text, size_t capacity, mybot_translation_direction_t direction) {
    const bool reverse = s_view.content.translation_mode == MYBOT_TRANSLATION_LISTEN_SPEAK &&
                         direction == MYBOT_TRANSLATION_DIRECTION_LISTEN;
    std::snprintf(
        text, capacity, "%s  >  %s",
        language_name(reverse ? s_view.content.target_language : s_view.content.source_language),
        language_name(reverse ? s_view.content.source_language : s_view.content.target_language));
}
void route_label(int y) {
    char text[128];
    if (face_session() && s_view.pending_direction >= 0)
        std::snprintf(text, sizeof(text), "%s", tr("正在切换发言语言", "Changing input language"));
    else if (face_session() &&
             std::strcmp(s_view.content.error_code, "DIRECTION_CHANGE_FAILED") == 0)
        std::snprintf(text, sizeof(text), "%s", tr("切换失败，请重试", "Could not switch. Retry."));
    else if (face_session() && s_view.content.input_direction == MYBOT_TRANSLATION_DIRECTION_NONE)
        std::snprintf(text, sizeof(text), "%s",
                      tr("请选择发言语言", "Choose the speaking language"));
    else if (meeting_session())
        std::snprintf(text, sizeof(text), "%s  <>  %s",
                      language_name(s_view.content.source_language),
                      language_name(s_view.content.target_language));
    else
        format_route(text, sizeof(text), s_view.content.input_direction);
    auto *route = label(s_view.body, margin(), y, compact() ? content_width() - 116 : 570,
                        compact() ? 52 : 36, text, &mybot_lvgl_font_20, LV_TEXT_ALIGN_LEFT);
    if (route)
        lv_label_set_long_mode(route, LV_LABEL_LONG_DOT);
    if (meeting_session())
        s_view.caption_route = route;
}
void live() {
    const bool face = face_session();
    heading(face                ? tr("面对面翻译", "Face-to-face translation")
            : meeting_session() ? tr("会议互译", "Meeting translation")
                                : tr("媒体翻译", "Media translation"),
            NoAction, tr("更多 ...", "More ..."), Menu);
    auto *card = button(margin(), compact() ? 74 : 82, content_width(), compact() ? 204 : 202, "",
                        Full, kWhite);
    const int text_width = content_width() - 40;
    s_view.caption_notice =
        label(card, 20, 12, text_width, 32, "", &mybot_lvgl_font_20, LV_TEXT_ALIGN_LEFT, kMuted);
    s_view.caption_target =
        label(card, 20, 58, text_width, 78, tr("等待字幕", "Waiting for captions"),
              &mybot_caption_font_28, LV_TEXT_ALIGN_LEFT);
    s_view.caption_source = label(card, 20, 140, text_width, compact() ? 52 : 30, "",
                                  &mybot_caption_font_20, LV_TEXT_ALIGN_LEFT);
    if (s_view.caption_target)
        lv_label_set_long_mode(s_view.caption_target, LV_LABEL_LONG_DOT);
    if (s_view.caption_source)
        lv_label_set_long_mode(s_view.caption_source, LV_LABEL_LONG_DOT);
    route_label(compact() ? 282 : 290);
    button(s_view.width - margin() - (compact() ? 108 : 148), 284, compact() ? 108 : 148, 44,
           s_view.content.paused ? tr("继续", "Resume") : tr("暂停", "Pause"), Pause, kPurple,
           !s_view.command_pending && s_view.pending_direction < 0 &&
               (!face || s_view.content.input_direction != MYBOT_TRANSLATION_DIRECTION_NONE));
    if (face) {
        const bool allow = s_view.pending_direction < 0 && !s_view.command_pending;
        const mybot_translation_direction_t directions[] = {MYBOT_TRANSLATION_DIRECTION_SPEAK,
                                                            MYBOT_TRANSLATION_DIRECTION_LISTEN};
        const char *codes[] = {s_view.content.source_language, s_view.content.target_language};
        for (unsigned i = 0; i < 2; ++i) {
            const int column = (content_width() - 24) / 2;
            auto *choice =
                button(margin() + i * (column + 24), 338, column, 76, "", i ? Listen : Speak,
                       s_view.content.input_direction == directions[i] ? kTeal : kWhite, allow);
            label(choice, 12, 12, column - 24, 52, language_name(codes[i]),
                  compact() ? &mybot_lvgl_font_20 : &mybot_lvgl_font_28);
        }
    } else {
        utilities(338);
    }
}
void menu() {
    heading(tr("翻译选项", "Translation options"), Live);
    button(margin(), 92, content_width(), 64, tr("本次字幕与全文  >", "Captions and full text  >"),
           History, kWhite);
    button(margin(), 174, content_width(), 64,
           compact() ? tr("输出音量", "Output volume")
                     : tr("音量与亮度  >", "Volume and brightness  >"),
           Sound, kMint);
    button(margin(), 256, content_width(), 64,
           tr("更改语言与使用方式  >", "Change languages and scene  >"), Change, kPurple);
    button(margin(), 338, content_width(), 64, tr("结束这次翻译", "End this translation"), End,
           kPink);
}
void captions(bool history) {
    heading(history ? tr("本次字幕历史", "Caption history") : tr("完整字幕", "Full caption"),
            history ? CaptionExit : History);
    if (history) {
        s_view.caption_notice = label(s_view.body, margin() + 8, 64, content_width() - 16, 30, "",
                                      &mybot_lvgl_font_20, LV_TEXT_ALIGN_LEFT, kMuted);
        if (s_view.caption_notice)
            lv_label_set_long_mode(s_view.caption_notice, LV_LABEL_LONG_DOT);
        for (unsigned i = 0; i < kCaptionsPerPage; ++i) {
            auto *card = button(margin(), 102 + i * 62, content_width(), 58, "",
                                static_cast<Action>(CaptionBase + i), kWhite, false, 12);
            s_view.history_buttons[i] = card;
            s_view.history_route[i] = label(card, 16, 2, content_width() - 32, 28, "",
                                            &mybot_lvgl_font_20, LV_TEXT_ALIGN_LEFT, kMuted);
            s_view.history_target[i] = label(card, 16, 29, content_width() - 32, 28, "",
                                             &mybot_caption_font_20, LV_TEXT_ALIGN_LEFT);
            if (s_view.history_target[i])
                lv_label_set_long_mode(s_view.history_target[i], LV_LABEL_LONG_DOT);
            if (s_view.history_route[i])
                lv_label_set_long_mode(s_view.history_route[i], LV_LABEL_LONG_DOT);
        }
        s_view.history_previous =
            button(margin(), compact() ? 354 : 370, compact() ? 120 : 190, compact() ? 64 : 44,
                   tr("< 上一页", "< Previous"), HistoryPrevious, kSoft, false);
        button(compact() ? 164 : 242, compact() ? 354 : 370, compact() ? 152 : 316,
               compact() ? 64 : 44,
               active() ? tr("返回实时翻译", "Back to live") : tr("返回结束页", "Back to summary"),
               CaptionExit, kTeal);
        s_view.history_next =
            button(s_view.width - margin() - (compact() ? 120 : 190), compact() ? 354 : 370,
                   compact() ? 120 : 190, compact() ? 64 : 44, tr("下一页 >", "Next >"),
                   HistoryNext, kSoft, false);
    } else {
        s_view.caption_route = label(s_view.body, margin() + 8, 64, content_width() - 16, 32, "",
                                     &mybot_lvgl_font_20, LV_TEXT_ALIGN_LEFT, kMuted);
        if (s_view.caption_route)
            lv_label_set_long_mode(s_view.caption_route, LV_LABEL_LONG_DOT);
        auto *card = panel(s_view.body, margin(), 104, content_width(), 242, kWhite);
        if (card) {
            lv_obj_add_flag(card, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_scroll_dir(card, LV_DIR_VER);
            lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_style_pad_all(card, 24, 0);
            lv_obj_set_style_pad_row(card, 16, 0);
        }
        s_view.caption_target = label(card, 0, 0, content_width() - 48, LV_SIZE_CONTENT, "",
                                      &mybot_caption_font_28, LV_TEXT_ALIGN_LEFT);
        s_view.caption_source = label(card, 0, 0, content_width() - 48, LV_SIZE_CONTENT, "",
                                      &mybot_caption_font_20, LV_TEXT_ALIGN_LEFT);
        s_view.caption_notice = label(card, 0, 0, content_width() - 48, LV_SIZE_CONTENT, "",
                                      &mybot_lvgl_font_20, LV_TEXT_ALIGN_LEFT, kMuted);
        button((s_view.width - 260) / 2, 362, 260, 48,
               active() ? tr("返回实时翻译", "Back to live") : tr("返回结束页", "Back to summary"),
               CaptionExit, kTeal);
    }
}
void caption_text(lv_obj_t *object, const char *text) {
    if (object)
        lv_label_set_text(object, text);
}
void refresh_captions() {
    if (!s_view.captions_dirty || !s_view.caption ||
        (s_view.page != Page::Live && s_view.page != Page::Full && s_view.page != Page::History))
        return;
    const auto &state = s_view.content;
    if (s_view.page == Page::Live && face_session() &&
        state.input_direction == MYBOT_TRANSLATION_DIRECTION_NONE) {
        caption_text(s_view.caption_target, tr("请选择发言语言", "Choose the speaking language"));
        caption_text(s_view.caption_source, tr("麦克风尚未开启", "Microphone input is off"));
        caption_text(s_view.caption_notice, "");
        s_view.captions_dirty = false;
        return;
    }
    if (!state.caption_count || (s_view.page == Page::Live && !live_caption_sequence())) {
        const char *notice = state.caption_generation ? tr("等待字幕", "Waiting for captions")
                                                      : tr("字幕暂不可用", "Captions unavailable");
        caption_text(s_view.caption_target, notice);
        caption_text(s_view.caption_source, s_view.page == Page::Live && meeting_session()
                                                ? tr("双方可同时发言", "Both sides can speak")
                                                : "");
        caption_text(s_view.caption_notice, "");
        for (unsigned i = 0; i < kCaptionsPerPage; ++i) {
            s_view.history_sequences[i] = 0;
            caption_text(s_view.history_target[i], "");
            caption_text(s_view.history_route[i], "");
            enabled(s_view.history_buttons[i], false);
        }
        if (s_view.page == Page::History)
            caption_text(s_view.caption_notice, notice);
        enabled(s_view.history_previous, false);
        enabled(s_view.history_next, false);
        s_view.captions_dirty = false;
        return;
    }
    if (s_view.page == Page::History) {
        if (s_view.history_first < state.caption_first_sequence ||
            s_view.history_first > state.caption_latest_sequence)
            s_view.history_first = state.caption_first_sequence;
        bool busy = false;
        bool truncated = false;
        for (unsigned i = 0; i < kCaptionsPerPage; ++i) {
            const uint32_t sequence = s_view.history_first + i;
            if (sequence > state.caption_latest_sequence) {
                s_view.history_sequences[i] = 0;
                caption_text(s_view.history_target[i], "");
                caption_text(s_view.history_route[i], "");
                enabled(s_view.history_buttons[i], false);
            } else if (mybot_ui_copy_caption(state.caption_generation, sequence, s_view.caption) ==
                       0) {
                s_view.history_sequences[i] = sequence;
                caption_text(s_view.history_target[i],
                             s_view.caption->target_text[0]
                                 ? s_view.caption->target_text
                                 : tr("等待译文", "Waiting for translation"));
                char route[128];
                format_route(route, sizeof(route), s_view.caption->direction);
                caption_text(s_view.history_route[i], route);
                enabled(s_view.history_buttons[i], true);
                truncated = truncated || s_view.caption->source_truncated ||
                            s_view.caption->target_truncated;
            } else {
                if (s_view.history_sequences[i] != sequence) {
                    s_view.history_sequences[i] = 0;
                    caption_text(s_view.history_target[i], "");
                    caption_text(s_view.history_route[i], "");
                    enabled(s_view.history_buttons[i], false);
                }
                busy = true;
            }
        }
        char notice[128];
        std::snprintf(
            notice, sizeof(notice),
            truncated ? tr("最近 %u 条 · 第 %u 页 · 文本过长，已截断",
                           "Recent %u · Page %u · Long text truncated")
                      : tr("最近 %u 条 · 第 %u 页", "Recent %u · Page %u"),
            static_cast<unsigned>(state.caption_count),
            static_cast<unsigned>(
                (s_view.history_first - state.caption_first_sequence) / kCaptionsPerPage + 1));
        caption_text(s_view.caption_notice, notice);
        enabled(s_view.history_previous, s_view.history_first > state.caption_first_sequence);
        enabled(s_view.history_next,
                state.caption_latest_sequence - s_view.history_first >= kCaptionsPerPage);
        s_view.captions_dirty = busy;
        return;
    }
    const uint32_t sequence =
        s_view.page == Page::Live ? live_caption_sequence() : s_view.selected_sequence;
    const bool evicted = s_view.page == Page::Full && (sequence < state.caption_first_sequence ||
                                                       sequence > state.caption_latest_sequence);
    /* The copied full-page snapshot remains readable after ring eviction,
     * including a local control redraw. Never substitute another sentence. */
    if (evicted && (!sequence || s_view.caption->sequence != sequence)) {
        caption_text(s_view.caption_target, tr("请返回字幕历史", "Return to caption history"));
        caption_text(s_view.caption_source, "");
        caption_text(s_view.caption_notice, "");
        s_view.captions_dirty = false;
        return;
    }
    if (!evicted && mybot_ui_copy_caption(state.caption_generation, sequence, s_view.caption) != 0)
        return; /* Nonblocking read: retain the last snapshot and retry on the owned timer. */
    s_view.selected_sequence = sequence;
    char route[128];
    format_route(route, sizeof(route), s_view.caption->direction);
    caption_text(s_view.caption_route, route);
    caption_text(s_view.caption_target, s_view.caption->target_text[0]
                                            ? s_view.caption->target_text
                                            : tr("等待译文", "Waiting for translation"));
    caption_text(s_view.caption_source, s_view.caption->source_text);
    caption_text(s_view.caption_notice,
                 s_view.caption->source_truncated || s_view.caption->target_truncated
                     ? tr("文本过长，已截断", "Long text truncated")
                 : s_view.caption->source_final && s_view.caption->target_final
                     ? tr("已定稿", "Final")
                     : tr("实时字幕", "Live captions"));
    s_view.captions_dirty = false;
}
void confirmation(bool change) {
    heading(change ? tr("更改设置需重新开始", "Changing settings requires a new session")
                   : tr("结束这次翻译？", "End this translation?"));
    label(s_view.body, margin() + 12, 114, content_width() - 24, 164,
          change ? tr("先结束当前连接，再更换使用方式或语言。",
                      "End the current session before changing languages or scene.")
                 : tr("确认后结束音频采集、译音播放和云端会话。",
                      "Confirm to end audio input, playback, and the cloud session."),
          &mybot_lvgl_font_28);
    button(margin(), 316, (content_width() - 24) / 2, 68, tr("继续翻译", "Keep translating"), Live,
           kWhite);
    button((s_view.width + 24) / 2, 316, (content_width() - 24) / 2, 68,
           change ? tr("结束并更换", "End and change") : tr("结束翻译", "End translation"),
           change ? ConfirmChange : ConfirmEnd, kPink);
}
struct Message {
    const char *title;
    const char *notice;
};
Message service_message() {
    if (active() && s_view.stop_requested)
        return {tr("正在结束翻译", "Ending translation"),
                tr("等待服务端确认停止", "Waiting for the service to confirm stop")};
    if (idle_error()) {
        if (std::strcmp(s_view.content.error_code, "USB_AUDIO_NOT_READY") == 0)
            return {tr("电脑音频未开启", "Computer audio is not ready"),
                    tr("请连接电脑，选择设备输入与输出并播放音频",
                       "Connect the computer, select this device as input and output, and play "
                       "audio.")};
        if (std::strcmp(s_view.content.error_code, "RTC_NOT_READY") == 0)
            return {tr("本地连接未恢复", "Local RTC is not ready"),
                    tr("请重试或重启设备", "Retry, or restart the device if this persists.")};
        if (std::strcmp(s_view.content.error_code, "MEDIA_NOT_READY") == 0)
            return {tr("本地音频未恢复", "Local audio is not ready"),
                    tr("本地音频未恢复，请重试", "Local audio has not recovered. Please retry.")};
        if (std::strcmp(s_view.content.error_code, "TIME_NOT_SYNCED") == 0)
            return {tr("时间尚未同步", "Time is not synchronized"),
                    tr("请等待时间同步后重试", "Wait for time synchronization, then retry.")};
        return {tr("无法开始翻译", "Could not start translation"),
                tr("服务暂时无法启动，请稍后重试",
                   "The service could not start. Please try again shortly.")};
    }
    switch (s_view.content.screen) {
    case MYBOT_LCD_SCREEN_STARTING:
        return {tr("启动中", "Starting"), tr("正在初始化", "Initializing")};
    case MYBOT_LCD_SCREEN_WIFI_PROVISIONING:
        return {tr("Wi-Fi 配网", "Wi-Fi setup"),
                tr("请连接设备热点完成配网", "Connect to the device hotspot to configure Wi-Fi")};
    case MYBOT_LCD_SCREEN_WIFI_DISCONNECTED:
        return {tr("网络已断开", "Wi-Fi disconnected"), tr("正在重新连接", "Reconnecting")};
    case MYBOT_LCD_SCREEN_STARTING_SERVICES:
        return {tr("连接服务中", "Connecting services"),
                tr("正在连接翻译服务", "Connecting to translation services")};
    case MYBOT_LCD_SCREEN_PAIRING:
        return {tr("等待配对", "Pairing"), tr("正在获取配对码", "Getting a pairing code")};
    case MYBOT_LCD_SCREEN_PAIR_CODE:
        return {tr("配对码", "Pairing code"),
                tr("在网页控制台输入配对码", "Enter this code in the web console")};
    case MYBOT_LCD_SCREEN_TRANSLATION_STARTING:
        return {s_view.cancel_requested ? tr("正在取消启动", "Cancelling start")
                                        : tr("正在连接翻译", "Connecting translation"),
                tr("等待服务端确认结果", "Waiting for the service to confirm the result")};
    case MYBOT_LCD_SCREEN_TRANSLATION_STOPPING:
        return {tr("正在结束翻译", "Ending translation"),
                tr("等待服务端确认停止", "Waiting for the service to confirm stop")};
    case MYBOT_LCD_SCREEN_CLEANUP_PENDING:
        return {tr("结束尚未完成", "Cleanup pending"),
                tr("完成结束后才能重新开始", "Finish cleanup before starting another session")};
    case MYBOT_LCD_SCREEN_START_UNKNOWN:
        return {tr("启动结果未知", "Start outcome unknown"),
                tr("会话可能已启动，请在控制台确认",
                   "Session may be active. Reconcile it in the console")};
    case MYBOT_LCD_SCREEN_TRANSLATION_ERROR:
        return {
            tr("翻译异常", "Translation error"),
            tr("检查错误信息后返回设置重试", "Check the error, then return to settings to retry")};
    case MYBOT_LCD_SCREEN_FAILED:
        return {tr("启动失败", "Startup failed"),
                tr("请重启后重试", "Restart the device to retry")};
    case MYBOT_LCD_SCREEN_STOPPING:
        return {tr("正在退出", "Shutting down"), tr("正在结束服务", "Closing services")};
    default:
        return {tr("请稍候", "Please wait"), tr("正在处理请求", "Processing the request")};
    }
}
void service() {
    const Message message = service_message();
    heading(message.title);
    label(s_view.body, margin() + 12, 102, content_width() - 24, 112, message.notice,
          &mybot_lvgl_font_28);
    const char *detail = s_view.content.error_code;
    char ssid[MYBOT_WIFI_PROVISIONING_SSID_CAPACITY]{};
    if (s_view.content.screen == MYBOT_LCD_SCREEN_PAIR_CODE)
        detail = s_view.content.pair_code;
    else if (s_view.content.screen == MYBOT_LCD_SCREEN_WIFI_PROVISIONING) {
        (void)mybot_wifi_get_provisioning_ssid(ssid, sizeof(ssid));
        detail = ssid;
    }
    label(s_view.body, margin(), 222, content_width(), 72, detail,
          s_view.content.screen == MYBOT_LCD_SCREEN_PAIR_CODE ? &mybot_lvgl_font_40
                                                              : &mybot_lvgl_font_20,
          LV_TEXT_ALIGN_CENTER, kWarning);
    if (s_view.content.screen == MYBOT_LCD_SCREEN_TRANSLATION_STARTING)
        button((s_view.width - 344) / 2, 330, 344, 62,
               s_view.cancel_requested ? tr("正在取消...", "Cancelling...")
                                       : tr("取消连接", "Cancel connection"),
               CancelStart, kWhite, !s_view.cancel_requested);
    else if (s_view.content.screen == MYBOT_LCD_SCREEN_CLEANUP_PENDING ||
             (active() && s_view.stop_requested))
        button((s_view.width - 344) / 2, 330, 344, 62, tr("重试结束", "Retry stop"), RetryStop,
               kPink, !s_view.command_pending);
    else if (idle_error())
        button((s_view.width - 344) / 2, 330, 344, 62, tr("返回设置", "Back to settings"), Setup,
               kTeal);
    else
        button((s_view.width - 344) / 2, 330, 344, 62, tr("请稍候", "Please wait"), NoAction, kSoft,
               false);
}
void redraw() {
    if (!s_view.body)
        return;
    s_view.home_time = s_view.home_date = s_view.volume_label = nullptr;
    s_view.caption_source = s_view.caption_target = s_view.caption_notice = nullptr;
    s_view.caption_route = nullptr;
    s_view.history_previous = s_view.history_next = nullptr;
    for (unsigned i = 0; i < kCaptionsPerPage; ++i) {
        s_view.history_route[i] = s_view.history_target[i] = s_view.history_buttons[i] = nullptr;
        s_view.history_sequences[i] = 0;
    }
    s_view.captions_dirty = true;
    s_view.volume_slider = Slider{};
    s_view.brightness_slider = Slider{};
    lv_obj_clean(s_view.body);
    s_view.failed = false;
    switch (s_view.page) {
    case Page::Home:
        home();
        break;
    case Page::Entry:
        entry();
        break;
    case Page::Setup:
        setup();
        break;
    case Page::Languages:
        languages();
        break;
    case Page::Live:
        live();
        break;
    case Page::Menu:
        menu();
        break;
    case Page::Sound:
        heading(compact() ? tr("输出音量", "Output volume")
                          : tr("音量与亮度", "Volume and brightness"),
                Live);
        utilities(144);
        if (!compact())
            label(s_view.body, 40, 264, 720, 52,
                  tr("亮度滑条仅供界面演示", "Brightness slider is a UI demo"));
        break;
    case Page::Full:
        captions(false);
        break;
    case Page::History:
        captions(true);
        break;
    case Page::EndConfirm:
        confirmation(false);
        break;
    case Page::ChangeConfirm:
        confirmation(true);
        break;
    case Page::Complete:
        heading(tr("翻译已结束", "Translation ended"), Home);
        label(s_view.body, margin(), 112, content_width(), 100,
              tr("服务端已确认结束", "The service has confirmed the session ended"),
              &mybot_lvgl_font_28);
        button((s_view.width - 320) / 2, 232, 320, 52, tr("查看本次字幕", "View captions"), History,
               kSoft, s_view.content.caption_count != 0);
        button(margin(), 312, (content_width() - 24) / 2, 68, tr("更换使用方式", "Change scene"),
               Entry, kWhite);
        button((s_view.width + 24) / 2, 312, (content_width() - 24) / 2, 68,
               tr("再来一次", "Translate again"), Setup, kTeal);
        break;
    case Page::Service:
        service();
        break;
    }
    update_clock(nullptr);
}
void command(mybot_ui_command_type_t type, int value) {
    mybot_ui_command_t request{};
    request.type = type;
    request.value = value;
    mybot_board_keys_ui_command(&request);
}
void stop(bool change) {
    if (!active())
        return;
    s_view.change_after_stop = change;
    s_view.stop_requested = true;
    mark_pending();
    s_view.page = Page::Service;
    redraw();
    mybot_board_keys_ui_event(MYBOT_KEY_EVENT_CONVERSATION_STOP);
}
void clicked(lv_event_t *event) {
    auto *target = lv_event_get_current_target_obj(event);
    if (!target || lv_obj_has_state(target, LV_STATE_DISABLED))
        return;
    const auto action =
        static_cast<Action>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
    if (action >= CaptionBase && action < CaptionBase + kCaptionsPerPage) {
        const unsigned index = static_cast<unsigned>(action - CaptionBase);
        if (s_view.page != Page::History || !s_view.history_sequences[index])
            return;
        s_view.selected_sequence = s_view.history_sequences[index];
        s_view.page = Page::Full;
        redraw();
        return;
    }
    if (action >= LanguageBase && action < CaptionBase) {
        if (!idle() || s_view.page != Page::Languages || !languages_allowed())
            return;
        size_t count = 0;
        const auto *list = mybot_ui_languages(&count);
        const auto index = static_cast<unsigned>(action - LanguageBase);
        if (index >= count)
            return;
        const char *source = s_view.pick_target ? source_code() : list[index].code;
        const char *destination = s_view.pick_target ? list[index].code : target_code();
        if (std::strcmp(source, destination) == 0)
            return;
        if (!meeting_preview()) {
            mybot_ui_command_t request{};
            request.type = MYBOT_UI_SET_LANGUAGES;
            std::snprintf(request.source_language, sizeof(request.source_language), "%s", source);
            std::snprintf(request.target_language, sizeof(request.target_language), "%s",
                          destination);
            /* A language pair must be read back before another pair is built.
             * Submit directly so a rejected mailbox cannot leave a permanent guard. */
            if (mybot_ui_submit(&request) == 0) {
                std::memcpy(s_view.pending_source, request.source_language,
                            sizeof(s_view.pending_source));
                std::memcpy(s_view.pending_target, request.target_language,
                            sizeof(s_view.pending_target));
            }
        } else {
            char source_copy[33]{}, target_copy[33]{};
            std::snprintf(source_copy, sizeof(source_copy), "%s", source);
            std::snprintf(target_copy, sizeof(target_copy), "%s", destination);
            std::memcpy(s_view.preview_source, source_copy, sizeof(source_copy));
            std::memcpy(s_view.preview_target, target_copy, sizeof(target_copy));
        }
        s_view.page = Page::Setup;
        redraw();
        return;
    }
    switch (action) {
    case Home:
        if (idle())
            s_view.page = Page::Home;
        break;
    case Entry:
        if (idle())
            s_view.page = Page::Entry;
        break;
    case Media:
    case Face:
    case Meeting: {
        if (!idle() || s_view.pending_mode >= 0 || s_view.command_pending || languages_pending())
            return;
        const Scene scene = action == Media  ? Scene::Media
                            : action == Face ? Scene::Face
                                             : Scene::Meeting;
        const auto mode =
            scene == Scene::Media ? MYBOT_TRANSLATION_LISTEN : MYBOT_TRANSLATION_LISTEN_SPEAK;
        const auto audio_source =
            scene == Scene::Meeting ? MYBOT_AUDIO_SOURCE_USB : MYBOT_AUDIO_SOURCE_CODEC;
        const bool preview = scene == Scene::Meeting && !s_view.content.usb_audio_available;
        if (!preview && (s_view.content.translation_mode != mode ||
                         s_view.content.audio_source != audio_source)) {
            mybot_ui_command_t request{};
            request.type = MYBOT_UI_SET_MODE;
            request.value = mode;
            request.audio_source = audio_source;
            if (mybot_ui_submit(&request) < 0)
                return;
            s_view.pending_mode = mode;
            s_view.pending_audio_source = audio_source;
            s_view.pending_selection_revision = s_view.content.selection_revision;
        }
        s_view.scene = scene;
        s_view.page = Page::Setup;
        break;
    }
    case Setup:
        if (idle())
            s_view.page = Page::Setup;
        break;
    case Source:
    case Target:
        if (!idle() || !languages_allowed())
            return;
        s_view.pick_target = action == Target;
        s_view.language_page = 0;
        s_view.page = Page::Languages;
        break;
    case LanguagePrevious:
        if (s_view.language_page)
            --s_view.language_page;
        break;
    case LanguageNext: {
        size_t count = 0;
        (void)mybot_ui_languages(&count);
        if ((s_view.language_page + 1) * kLanguagesPerPage < count)
            ++s_view.language_page;
        break;
    }
    case Start:
        if (!idle() || !mode_ready() || s_view.command_pending || languages_pending())
            return;
        mark_pending();
        s_view.start_requested = true;
        mybot_board_keys_ui_event(MYBOT_KEY_EVENT_CONVERSATION_START);
        break;
    case CancelStart:
        if (s_view.content.screen != MYBOT_LCD_SCREEN_TRANSLATION_STARTING ||
            s_view.cancel_requested)
            return;
        s_view.cancel_requested = true;
        s_view.stop_requested = true;
        mybot_board_keys_ui_event(MYBOT_KEY_EVENT_CONVERSATION_STOP);
        break;
    case Menu:
        if (active())
            s_view.page = Page::Menu;
        break;
    case Live:
        if (active() && !s_view.stop_requested)
            s_view.page = Page::Live;
        break;
    case Sound:
        if (active())
            s_view.page = Page::Sound;
        break;
    case Full:
        if (active() && live_caption_sequence()) {
            s_view.selected_sequence = live_caption_sequence();
            s_view.page = Page::Full;
        }
        break;
    case History:
        if (active() || (idle() && s_view.content.caption_count)) {
            if (s_view.page != Page::Full)
                s_view.history_first = s_view.content.caption_first_sequence;
            s_view.page = Page::History;
        }
        break;
    case CaptionExit:
        s_view.page = active() ? Page::Live : Page::Complete;
        break;
    case HistoryPrevious:
        if (s_view.page == Page::History &&
            s_view.history_first > s_view.content.caption_first_sequence) {
            const uint32_t distance = s_view.history_first - s_view.content.caption_first_sequence;
            s_view.history_first -= distance < kCaptionsPerPage ? distance : kCaptionsPerPage;
        }
        break;
    case HistoryNext:
        if (s_view.page == Page::History &&
            s_view.content.caption_latest_sequence - s_view.history_first >= kCaptionsPerPage)
            s_view.history_first += kCaptionsPerPage;
        break;
    case Speak:
    case Listen: {
        if (!active() || !face_session() || s_view.pending_direction >= 0 || s_view.command_pending)
            return;
        const auto direction = action == Speak ? MYBOT_TRANSLATION_DIRECTION_SPEAK
                                               : MYBOT_TRANSLATION_DIRECTION_LISTEN;
        if (s_view.content.input_direction == direction && !s_view.content.paused)
            return;
        mybot_ui_command_t request{};
        request.type = MYBOT_UI_SET_DIRECTION;
        request.value = direction;
        if (mybot_ui_submit(&request) < 0)
            return;
        s_view.pending_direction = direction;
        s_view.pending_selection_revision = s_view.content.selection_revision;
        break;
    }
    case Pause:
        if (!active() || s_view.command_pending || s_view.pending_direction >= 0 ||
            (face_session() && s_view.content.input_direction == MYBOT_TRANSLATION_DIRECTION_NONE))
            return;
        mark_pending();
        command(MYBOT_UI_SET_PAUSED, !s_view.content.paused);
        break;
    case End:
        if (active())
            s_view.page = Page::EndConfirm;
        break;
    case Change:
        if (active())
            s_view.page = Page::ChangeConfirm;
        break;
    case ConfirmEnd:
        stop(false);
        return;
    case ConfirmChange:
        stop(true);
        return;
    case RetryStop:
        if ((s_view.content.screen != MYBOT_LCD_SCREEN_CLEANUP_PENDING &&
             !(active() && s_view.stop_requested)) ||
            s_view.command_pending)
            return;
        s_view.stop_requested = true;
        mark_pending();
        mybot_board_keys_ui_event(MYBOT_KEY_EVENT_CONVERSATION_STOP);
        break;
    default:
        return;
    }
    redraw();
}
} // namespace

int mybot_lvgl_view_create_sized(lv_display_t *display, int width, int height) {
    /* Both layouts retain native font and touch sizes; the square panel reflows
     * scene cards and utility controls instead of shrinking the wide view. */
    if (!display || s_view.root || (width != 800 && width != 480) || height != 480 ||
        lv_display_get_horizontal_resolution(display) != width ||
        lv_display_get_vertical_resolution(display) != height)
        return -1;
    s_view.width = width;
    s_view.content.screen = MYBOT_LCD_SCREEN_STARTING;
    s_view.content.volume = -1;
    s_view.root = panel(lv_display_get_screen_active(display), 0, 0, width, height, kBackground, 0);
    /* The Mosaico cover glass masks the panel corners. Keep the status strip
     * inside the straight top edge; body controls also leave a wider margin. */
    const int status_inset = compact() ? 60 : margin();
    s_view.network = label(s_view.root, status_inset, compact() ? 16 : 14, compact() ? 240 : 260,
                           30, "", &mybot_lvgl_font_20, LV_TEXT_ALIGN_LEFT);
    if (!compact())
        label(s_view.root, 300, 14, 200, 30, "BotStation");
    const int clock_width = compact() ? 100 : 120;
    s_view.clock = label(s_view.root, width - status_inset - clock_width, compact() ? 16 : 14,
                         clock_width, 30, "--:--", &mybot_lvgl_font_20, LV_TEXT_ALIGN_RIGHT);
    s_view.body = panel(s_view.root, 0, 52, width, height - 52, kBackground, 0);
    if (!s_view.root || !s_view.network || !s_view.clock || !s_view.body || s_view.failed) {
        mybot_lvgl_view_destroy();
        return -1;
    }
    s_view.caption = static_cast<mybot_caption_t *>(lv_malloc(sizeof(mybot_caption_t)));
    if (!s_view.caption) {
        mybot_lvgl_view_destroy();
        return -1;
    }
    redraw();
    s_view.timer = lv_timer_create(update_clock, 1000, nullptr);
    if (!s_view.timer || s_view.failed) {
        mybot_lvgl_view_destroy();
        return -1;
    }
    return 0;
}
int mybot_lvgl_view_create(lv_display_t *display) {
    return display ? mybot_lvgl_view_create_sized(display,
                                                  lv_display_get_horizontal_resolution(display),
                                                  lv_display_get_vertical_resolution(display))
                   : -1;
}
void mybot_lvgl_view_update(const mybot_lcd_content_t *content) {
    if (!s_view.root || !content || content->screen < MYBOT_LCD_SCREEN_STARTING ||
        content->screen >= MYBOT_LCD_SCREEN_COUNT)
        return;
    const auto previous = s_view.content;
    const bool changed = previous.screen != content->screen;
    const bool language_changed =
        std::strcmp(previous.source_language, content->source_language) != 0 ||
        std::strcmp(previous.target_language, content->target_language) != 0;
    const bool paused_changed = previous.paused != content->paused;
    const bool mode_changed = previous.translation_mode != content->translation_mode ||
                              previous.audio_source != content->audio_source;
    const bool capability_changed = previous.usb_audio_available != content->usb_audio_available;
    const bool direction_changed = previous.input_direction != content->input_direction;
    const bool error_changed = std::strcmp(previous.error_code, content->error_code) != 0;
    const bool detail_changed =
        error_changed || std::strcmp(previous.pair_code, content->pair_code) != 0;
    const bool captions_changed = previous.caption_generation != content->caption_generation ||
                                  previous.caption_revision != content->caption_revision;
    const bool captions_only =
        captions_changed && !changed && !language_changed && !paused_changed && !mode_changed &&
        !capability_changed && !direction_changed && !detail_changed &&
        previous.volume == content->volume && previous.indicators == content->indicators &&
        previous.selection_revision == content->selection_revision;
    const bool was_pending = s_view.command_pending || languages_pending();
    const int previous_pending_mode = s_view.pending_mode;
    const int previous_pending_direction = s_view.pending_direction;
    s_view.content = *content;
    const bool selection_finished =
        content->selection_revision != s_view.pending_selection_revision;
    if (s_view.pending_mode >= 0 &&
        (!idle() ||
         (selection_finished && ((content->translation_mode == s_view.pending_mode &&
                                  content->audio_source == s_view.pending_audio_source) ||
                                 content->error_code[0])))) {
        s_view.pending_mode = -1;
        s_view.pending_audio_source = -1;
    }
    if (s_view.pending_direction >= 0 &&
        (!active() ||
         (selection_finished &&
          ((content->input_direction == s_view.pending_direction && !content->paused) ||
           content->error_code[0]))))
        s_view.pending_direction = -1;
    const bool selection_acknowledged = previous_pending_mode != s_view.pending_mode ||
                                        previous_pending_direction != s_view.pending_direction;
    if (previous.caption_generation != content->caption_generation) {
        s_view.history_first = content->caption_first_sequence;
        s_view.selected_sequence = 0;
        if (s_view.caption)
            s_view.caption->sequence = 0;
        caption_text(s_view.caption_target, tr("等待字幕", "Waiting for captions"));
        caption_text(s_view.caption_source, "");
        caption_text(s_view.caption_notice, "");
        caption_text(s_view.caption_route, "");
        for (unsigned i = 0; i < kCaptionsPerPage; ++i) {
            caption_text(s_view.history_target[i], "");
            caption_text(s_view.history_route[i], "");
            enabled(s_view.history_buttons[i], false);
            s_view.history_sequences[i] = 0;
        }
    }
    if (captions_changed || direction_changed)
        s_view.captions_dirty = true;
    /* Unrelated presenter updates and the local retry timer are not language
     * acknowledgements. Only the requested pair or an actual failure releases it. */
    if (languages_pending() &&
        ((std::strcmp(content->source_language, s_view.pending_source) == 0 &&
          std::strcmp(content->target_language, s_view.pending_target) == 0) ||
         !idle() || (changed && content->screen == MYBOT_LCD_SCREEN_TRANSLATION_ERROR) ||
         std::strcmp(content->error_code, "STORAGE_ERROR") == 0 ||
         (error_changed && content->error_code[0])))
        s_view.pending_source[0] = '\0';
    /* Other controls use a local retry guard; only SDK state may show success. */
    if (!captions_only)
        s_view.command_pending = false;
    if (changed || (s_view.page == Page::Service && completed_cleanup_hint())) {
        if (content->screen == MYBOT_LCD_SCREEN_READY) {
            s_view.page =
                s_view.stop_requested
                    ? (s_view.change_after_stop || s_view.cancel_requested ? Page::Setup
                                                                           : Page::Complete)
                    : (changed ? Page::Home : Page::Setup);
            s_view.stop_requested = s_view.change_after_stop = s_view.cancel_requested = false;
            if (idle_error())
                s_view.page = Page::Service;
        } else if (content->screen == MYBOT_LCD_SCREEN_IN_CONVERSATION) {
            s_view.scene = face_session()      ? Scene::Face
                           : meeting_session() ? Scene::Meeting
                                               : Scene::Media;
            s_view.page = s_view.stop_requested ? Page::Service : Page::Live;
        } else {
            s_view.page = Page::Service;
        }
    }
    if (!changed && (detail_changed || s_view.start_requested || selection_acknowledged) &&
        idle_error())
        s_view.page = Page::Service;
    s_view.start_requested = false;
    if (changed || language_changed || paused_changed || detail_changed || mode_changed ||
        capability_changed || selection_acknowledged ||
        (direction_changed && s_view.page == Page::Live) || (was_pending && !captions_only) ||
        (s_view.page == Page::Setup || s_view.page == Page::Service))
        redraw();
    else {
        update_volume();
        update_clock(nullptr);
    }
}
void mybot_lvgl_view_destroy(void) {
    if (s_view.timer)
        lv_timer_delete(s_view.timer);
    if (s_view.root)
        lv_obj_delete(s_view.root);
    if (s_view.caption)
        lv_free(s_view.caption);
    s_view = View{};
}
