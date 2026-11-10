/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Project Contributors */
#include "display/lvgl_view.h"
#include "sdkconfig.h"
#include "ui/ui_port.h"

#include <cstdio>
#include <cstring>
#include <ctime>

LV_FONT_DECLARE(mybot_lvgl_font_20);
LV_FONT_DECLARE(mybot_lvgl_font_28);
LV_FONT_DECLARE(mybot_lvgl_font_40);
LV_FONT_DECLARE(mybot_caption_font_20);
LV_FONT_DECLARE(mybot_caption_font_28);

namespace {
constexpr int kDisplayWidth = 480;
constexpr int kDisplayHeight = 480;
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
    StartRecoveryConfirm,
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
    Swap,
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
    StartRecovery,
    ConfirmStartRecovery,
    CancelStartRecovery,
    CaptionExit,
    HistoryPrevious,
    HistoryNext,
    LanguagePrevious,
    LanguageNext,
    LanguageBase = 100,
    CaptionBase = 200
};
struct Slider {
    lv_obj_t *object{};
    lv_obj_t *fill{};
    lv_obj_t *knob{};
    int width = 0;
    int value = 0;
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
};
View s_view;

int margin() {
    return 32;
}
int content_width() {
    return kDisplayWidth - 2 * margin();
}

bool idle() {
    return s_view.content.screen == MYBOT_LCD_SCREEN_READY ||
           s_view.content.screen == MYBOT_LCD_SCREEN_TRANSLATION_ERROR;
}
bool active() {
    return s_view.content.screen == MYBOT_LCD_SCREEN_IN_CONVERSATION;
}
bool start_recovery_available() {
    return s_view.content.screen == MYBOT_LCD_SCREEN_START_UNKNOWN;
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
mybot_translation_mode_t scene_mode() {
    return s_view.scene == Scene::Media ? MYBOT_TRANSLATION_LISTEN : MYBOT_TRANSLATION_LISTEN_SPEAK;
}
mybot_audio_source_t scene_audio_source() {
    return s_view.scene == Scene::Meeting ? MYBOT_AUDIO_SOURCE_USB : MYBOT_AUDIO_SOURCE_CODEC;
}
bool mode_ready() {
    return (s_view.scene != Scene::Meeting || s_view.content.usb_audio_available) &&
           s_view.pending_mode < 0 && s_view.content.translation_mode == scene_mode() &&
           s_view.content.audio_source == scene_audio_source();
}
bool languages_allowed() {
    return mode_ready() && !s_view.command_pending && !languages_pending();
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
int command(mybot_ui_command_type_t type, int value);

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
    const int side = 96;
    const int inset = margin();
    if (back != NoAction)
        button(inset, 10, side, 44, tr("< 返回", "< Back"), back, kSoft);
    const int left = back != NoAction ? inset + side + 8 : inset;
    const int edge = right ? inset + side + 8 : inset;
    label(s_view.body, left, 17, kDisplayWidth - left - edge, 48, title, &mybot_lvgl_font_20);
    if (right)
        button(kDisplayWidth - inset - side, 10, side, 44, right, right_action, kSoft);
}
const char *language_name(const char *code) {
    size_t count = 0;
    const mybot_ui_language_t *languages = mybot_ui_port_languages(&count);
    for (size_t i = 0; i < count; ++i)
        if (std::strcmp(code, languages[i].code) == 0)
            return tr(languages[i].name_zh, languages[i].name_en);
    return code[0] ? code : "--";
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
    if (timer && s_view.command_pending && s_view.page != Page::StartRecoveryConfirm &&
        lv_tick_get() - s_view.pending_since >= 1500) {
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
        lv_label_set_text(s_view.network, mybot_ui_port_wifi_connected()
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
void slider(int x, int y, int width) {
    lv_obj_t *card = panel(s_view.body, x, y, width, 86, kMint, 22);
    s_view.volume_label = label(card, 14, 8, width - 28, 28, "");
    Slider &control = s_view.volume_slider;
    control.width = width - 60;
    control.object = panel(card, 30, 38, control.width, 44, kMint, 0);
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
    set_slider(control, s_view.content.volume);
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
        set_slider(*control, s_view.content.volume);
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
        if (volume_allowed()) {
            command(MYBOT_UI_SET_VOLUME, control->value);
            update_volume();
        }
    }
}

void utilities(int y) {
    slider(margin(), y > 326 ? 326 : y, content_width());
    update_volume();
}
void home() {
    const int left = margin();
    label(s_view.body, left, 12, content_width(), 38,
          tr("你好，今天也有新发现", "Hello, Sunshine!"), &mybot_lvgl_font_28, LV_TEXT_ALIGN_LEFT);
    s_view.home_date =
        label(s_view.body, left, 58, 360, 30, "", &mybot_lvgl_font_20, LV_TEXT_ALIGN_LEFT, kMuted);
    s_view.home_time =
        label(s_view.body, left, 94, 340, 56, "", &mybot_lvgl_font_40, LV_TEXT_ALIGN_LEFT);
    auto *orb = panel(s_view.body, margin(), 174, content_width(), 96, kTeal, 24);
    if (orb) {
        lv_obj_add_flag(orb, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(orb, clicked, LV_EVENT_CLICKED, reinterpret_cast<void *>(Entry));
        label(orb, 18, 28, content_width() - 36, 40, tr("开始翻译 >", "Start translation >"),
              &mybot_lvgl_font_28);
    }
    utilities(286);
}
void entry() {
    heading(tr("选择翻译方式", "Choose a translation scene"), Home);
    const char *names[] = {tr("媒体翻译", "Listen"), tr("面对面", "In person"),
                           tr("会议互译", "Meeting")};
    const char *notes[] = {tr("单向听译", "One way"), tr("轮流交流", "Take turns"),
                           s_view.content.usb_audio_available
                               ? tr("双向会议翻译", "Two-way meeting audio")
                               : tr("USB 不可用", "USB unavailable")};
    const Action actions[] = {Media, Face, Meeting};
    const uint32_t colors[] = {kTeal, kPurple, kYellow};
    for (unsigned i = 0; i < 3; ++i) {
        auto *card =
            button(margin(), 78 + i * 112, content_width(), 100, "", actions[i], colors[i],
                   s_view.pending_mode < 0 && !s_view.command_pending && !languages_pending() &&
                       (i != 2 || s_view.content.usb_audio_available));
        label(card, 12, 12, content_width() - 24, 40, names[i], &mybot_lvgl_font_28);
        label(card, 12, 58, content_width() - 24, 30, notes[i], &mybot_lvgl_font_20);
    }
}
void setup() {
    heading(scene_name(), Entry);
    const int column = 176;
    const int left = margin();
    const int right = kDisplayWidth - left - column;
    label(s_view.body, left, 78, column, 30,
          s_view.scene == Scene::Media ? tr("输入语言", "Input language")
                                       : tr("我的语言", "My language"));
    label(s_view.body, right, 78, column, 30,
          s_view.scene == Scene::Media ? tr("翻译为", "Translate to")
                                       : tr("对方语言", "Other language"));
    button(left, 116, column, 96, language_name(s_view.content.source_language), Source, kTeal,
           languages_allowed());
    button(left + column, 132, right - left - column, 64, "<>", Swap, kWhite,
           idle() && languages_allowed());
    button(right, 116, column, 96, language_name(s_view.content.target_language), Target, kPurple,
           languages_allowed());
    label(s_view.body, margin(), 236, content_width(), 76,
          s_view.scene == Scene::Meeting && !s_view.content.usb_audio_available
              ? tr("USB 不可用", "USB unavailable")
          : s_view.pending_mode >= 0 ? tr("正在切换翻译方式", "Changing translation mode")
          : s_view.scene == Scene::Media
              ? tr("板载麦克风拾音，板载扬声器播放译音",
                   "Board microphone input and speaker translation output")
          : s_view.scene == Scene::Face
              ? tr("连接后选择发言语言，双方轮流交流",
                   "Connect, then select each speaker's language to take turns")
              : tr("电脑选择设备输入与输出，双方可同时发言",
                   "Select this device for computer input and output. Both sides can speak."),
          &mybot_lvgl_font_20, LV_TEXT_ALIGN_CENTER, kMuted);
    button((kDisplayWidth - 364) / 2, 328, 364, 64, tr("开始翻译  >", "Start translation  >"),
           Start, kTeal, mode_ready() && idle() && !s_view.command_pending && !languages_pending());
}
void languages() {
    heading(s_view.scene == Scene::Media ? (s_view.pick_target ? tr("翻译为", "Translate to")
                                                               : tr("输入语言", "Input language"))
                                         : (s_view.pick_target ? tr("对方语言", "Other language")
                                                               : tr("我的语言", "My language")),
            Setup);
    size_t count = 0;
    const auto *list = mybot_ui_port_languages(&count);
    const unsigned begin = s_view.language_page * kLanguagesPerPage;
    const char *other =
        s_view.pick_target ? s_view.content.source_language : s_view.content.target_language;
    const char *selected =
        s_view.pick_target ? s_view.content.target_language : s_view.content.source_language;
    for (unsigned i = 0; i < kLanguagesPerPage && begin + i < count; ++i) {
        const auto &item = list[begin + i];
        const bool chosen = std::strcmp(item.code, selected) == 0;
        constexpr int gap = 16;
        const int column = (content_width() - gap) / 2;
        button(margin() + (i % 2) * (column + gap), 84 + (i / 2) * 88, column, 72,
               tr(item.name_zh, item.name_en), static_cast<Action>(LanguageBase + begin + i),
               chosen ? kTeal : kWhite, std::strcmp(item.code, other) != 0 && languages_allowed());
    }
    button(margin(), 358, 142, 48, tr("< 上一页", "< Previous"), LanguagePrevious, kSoft,
           s_view.language_page > 0);
    char text[32];
    std::snprintf(text, sizeof(text), "%u / %u", s_view.language_page + 1,
                  static_cast<unsigned>((count + kLanguagesPerPage - 1) / kLanguagesPerPage));
    label(s_view.body, (kDisplayWidth - 120) / 2, 366, 120, 32, text);
    button(kDisplayWidth - margin() - 142, 358, 142, 48, tr("下一页 >", "Next >"), LanguageNext,
           kSoft, begin + kLanguagesPerPage < count);
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
    auto *route = label(s_view.body, margin(), y, content_width() - 116, 52, text,
                        &mybot_lvgl_font_20, LV_TEXT_ALIGN_LEFT);
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
    auto *card = button(margin(), 74, content_width(), 204, "", Full, kWhite);
    const int text_width = content_width() - 40;
    s_view.caption_notice =
        label(card, 20, 12, text_width, 32, "", &mybot_lvgl_font_20, LV_TEXT_ALIGN_LEFT, kMuted);
    s_view.caption_target =
        label(card, 20, 58, text_width, 78, tr("等待字幕", "Waiting for captions"),
              &mybot_caption_font_28, LV_TEXT_ALIGN_LEFT);
    s_view.caption_source =
        label(card, 20, 140, text_width, 52, "", &mybot_caption_font_20, LV_TEXT_ALIGN_LEFT);
    if (s_view.caption_target)
        lv_label_set_long_mode(s_view.caption_target, LV_LABEL_LONG_DOT);
    if (s_view.caption_source)
        lv_label_set_long_mode(s_view.caption_source, LV_LABEL_LONG_DOT);
    route_label(282);
    button(kDisplayWidth - margin() - 108, 284, 108, 44,
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
            label(choice, 12, 12, column - 24, 52, language_name(codes[i]), &mybot_lvgl_font_20);
        }
    } else {
        utilities(338);
    }
}
void menu() {
    heading(tr("翻译选项", "Translation options"), Live);
    button(margin(), 92, content_width(), 64, tr("本次字幕与全文  >", "Captions and full text  >"),
           History, kWhite);
    button(margin(), 174, content_width(), 64, tr("输出音量", "Output volume"), Sound, kMint);
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
        s_view.history_previous = button(margin(), 354, 120, 64, tr("< 上一页", "< Previous"),
                                         HistoryPrevious, kSoft, false);
        button(164, 354, 152, 64,
               active() ? tr("返回实时翻译", "Back to live") : tr("返回结束页", "Back to summary"),
               CaptionExit, kTeal);
        s_view.history_next = button(kDisplayWidth - margin() - 120, 354, 120, 64,
                                     tr("下一页 >", "Next >"), HistoryNext, kSoft, false);
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
        button((kDisplayWidth - 260) / 2, 362, 260, 48,
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
            } else if (mybot_ui_port_copy_caption(state.caption_generation, sequence,
                                                  s_view.caption) == 0) {
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
    if (!evicted &&
        mybot_ui_port_copy_caption(state.caption_generation, sequence, s_view.caption) != 0)
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
    button((kDisplayWidth + 24) / 2, 316, (content_width() - 24) / 2, 68,
           change ? tr("结束并更换", "End and change") : tr("结束翻译", "End translation"),
           change ? ConfirmChange : ConfirmEnd, kPink);
}
void start_recovery_confirmation() {
    const bool failed = s_view.content.screen == MYBOT_LCD_SCREEN_TRANSLATION_ERROR;
    heading(failed                   ? tr("恢复失败", "Recovery failed")
            : s_view.command_pending ? tr("正在恢复设备", "Recovering device")
                                     : tr("确认恢复设备？", "Recover this device?"));
    label(s_view.body, margin() + 12, 108, content_width() - 24, 128,
          failed ? tr("请重启后重试", "Restart the device to retry")
                 : tr("请先在网页控制台结束这台设备的全部会话，再确认恢复设备。",
                      "End every session for this device in the web console before confirming "
                      "recovery."),
          &mybot_lvgl_font_20);
    s_view.caption_notice =
        label(s_view.body, margin(), 244, content_width(), 56, s_view.content.error_code,
              &mybot_lvgl_font_20, LV_TEXT_ALIGN_CENTER, kWarning);
    button(margin(), 320, (content_width() - 24) / 2, 68, tr("返回", "Back"), CancelStartRecovery,
           kWhite, !s_view.command_pending);
    button((kDisplayWidth + 24) / 2, 320, (content_width() - 24) / 2, 68,
           s_view.command_pending ? tr("正在恢复...", "Recovering...")
                                  : tr("确认恢复", "Confirm recovery"),
           ConfirmStartRecovery, kTeal, start_recovery_available() && !s_view.command_pending);
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
    char ssid[MYBOT_UI_PORT_SSID_CAPACITY]{};
    if (s_view.content.screen == MYBOT_LCD_SCREEN_PAIR_CODE)
        detail = s_view.content.pair_code;
    else if (s_view.content.screen == MYBOT_LCD_SCREEN_WIFI_PROVISIONING) {
        (void)mybot_ui_port_provisioning_ssid(ssid, sizeof(ssid));
        detail = ssid;
    }
    label(s_view.body, margin(), 222, content_width(), 72, detail,
          s_view.content.screen == MYBOT_LCD_SCREEN_PAIR_CODE ? &mybot_lvgl_font_40
                                                              : &mybot_lvgl_font_20,
          LV_TEXT_ALIGN_CENTER, kWarning);
    if (s_view.content.screen == MYBOT_LCD_SCREEN_TRANSLATION_STARTING)
        button((kDisplayWidth - 344) / 2, 330, 344, 62,
               s_view.cancel_requested ? tr("正在取消...", "Cancelling...")
                                       : tr("取消连接", "Cancel connection"),
               CancelStart, kWhite, !s_view.cancel_requested);
    else if (s_view.content.screen == MYBOT_LCD_SCREEN_CLEANUP_PENDING ||
             (active() && s_view.stop_requested))
        button((kDisplayWidth - 344) / 2, 330, 344, 62, tr("重试结束", "Retry stop"), RetryStop,
               kPink, !s_view.command_pending);
    else if (start_recovery_available())
        button((kDisplayWidth - 344) / 2, 330, 344, 62,
               tr("控制台已结束，恢复设备", "Console sessions ended"), StartRecovery, kTeal);
    else if (idle_error())
        button((kDisplayWidth - 344) / 2, 330, 344, 62, tr("返回设置", "Back to settings"), Setup,
               kTeal);
    else
        button((kDisplayWidth - 344) / 2, 330, 344, 62, tr("请稍候", "Please wait"), NoAction,
               kSoft, false);
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
        heading(tr("输出音量", "Output volume"), Live);
        utilities(144);
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
    case Page::StartRecoveryConfirm:
        start_recovery_confirmation();
        break;
    case Page::Complete:
        heading(tr("翻译已结束", "Translation ended"), Home);
        label(s_view.body, margin(), 112, content_width(), 100,
              tr("服务端已确认结束", "The service has confirmed the session ended"),
              &mybot_lvgl_font_28);
        button((kDisplayWidth - 320) / 2, 232, 320, 52, tr("查看本次字幕", "View captions"),
               History, kSoft, s_view.content.caption_count != 0);
        button(margin(), 312, (content_width() - 24) / 2, 68, tr("更换使用方式", "Change scene"),
               Entry, kWhite);
        button((kDisplayWidth + 24) / 2, 312, (content_width() - 24) / 2, 68,
               tr("再来一次", "Translate again"), Setup, kTeal);
        break;
    case Page::Service:
        service();
        break;
    }
    update_clock(nullptr);
}
int command(mybot_ui_command_type_t type, int value) {
    mybot_ui_command_t request{};
    request.type = type;
    request.value = value;
    return mybot_ui_port_command(&request);
}
void submit_languages(const char *source, const char *target) {
    if (std::strcmp(source, target) == 0)
        return;
    mybot_ui_command_t request{};
    request.type = MYBOT_UI_SET_LANGUAGES;
    std::snprintf(request.source_language, sizeof(request.source_language), "%s", source);
    std::snprintf(request.target_language, sizeof(request.target_language), "%s", target);
    /* Wait for the complete pair before accepting another edit or swap. A rejected
     * mailbox leaves the controls available for an immediate retry. */
    if (mybot_ui_port_command(&request) == 0) {
        std::memcpy(s_view.pending_source, request.source_language, sizeof(s_view.pending_source));
        std::memcpy(s_view.pending_target, request.target_language, sizeof(s_view.pending_target));
    }
}
void stop(bool change) {
    if (!active())
        return;
    s_view.change_after_stop = change;
    s_view.stop_requested = true;
    mark_pending();
    s_view.page = Page::Service;
    redraw();
    mybot_ui_port_event(MYBOT_KEY_EVENT_CONVERSATION_STOP);
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
        const auto *list = mybot_ui_port_languages(&count);
        const auto index = static_cast<unsigned>(action - LanguageBase);
        if (index >= count)
            return;
        const char *source = s_view.pick_target ? s_view.content.source_language : list[index].code;
        const char *destination =
            s_view.pick_target ? list[index].code : s_view.content.target_language;
        submit_languages(source, destination);
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
        if (scene == Scene::Meeting && !s_view.content.usb_audio_available)
            return;
        if (s_view.content.translation_mode != mode ||
            s_view.content.audio_source != audio_source) {
            mybot_ui_command_t request{};
            request.type = MYBOT_UI_SET_MODE;
            request.value = mode;
            request.audio_source = audio_source;
            if (mybot_ui_port_command(&request) < 0)
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
    case Swap:
        if (s_view.page != Page::Setup || !idle() || !languages_allowed())
            return;
        submit_languages(s_view.content.target_language, s_view.content.source_language);
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
        (void)mybot_ui_port_languages(&count);
        if ((s_view.language_page + 1) * kLanguagesPerPage < count)
            ++s_view.language_page;
        break;
    }
    case Start:
        if (!idle() || !mode_ready() || s_view.command_pending || languages_pending())
            return;
        mark_pending();
        s_view.start_requested = true;
        mybot_ui_port_event(MYBOT_KEY_EVENT_CONVERSATION_START);
        break;
    case CancelStart:
        if (s_view.content.screen != MYBOT_LCD_SCREEN_TRANSLATION_STARTING ||
            s_view.cancel_requested)
            return;
        s_view.cancel_requested = true;
        s_view.stop_requested = true;
        mybot_ui_port_event(MYBOT_KEY_EVENT_CONVERSATION_STOP);
        break;
    case StartRecovery:
        if (!start_recovery_available() || s_view.command_pending)
            return;
        s_view.page = Page::StartRecoveryConfirm;
        break;
    case CancelStartRecovery:
        if (s_view.page != Page::StartRecoveryConfirm || s_view.command_pending)
            return;
        s_view.page = Page::Service;
        break;
    case ConfirmStartRecovery:
        if (s_view.page != Page::StartRecoveryConfirm || !start_recovery_available() ||
            s_view.command_pending)
            return;
        if (command(MYBOT_UI_CONFIRM_START_RECOVERY, 1) < 0) {
            redraw();
            caption_text(s_view.caption_notice, tr("暂时无法恢复，请稍后重试",
                                                   "Could not submit recovery. Please retry."));
            return;
        }
        s_view.pending_selection_revision = s_view.content.selection_revision;
        mark_pending();
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
        if (mybot_ui_port_command(&request) < 0)
            return;
        s_view.pending_direction = direction;
        s_view.pending_selection_revision = s_view.content.selection_revision;
        break;
    }
    case Pause:
        if (!active() || s_view.command_pending || s_view.pending_direction >= 0 ||
            (face_session() && s_view.content.input_direction == MYBOT_TRANSLATION_DIRECTION_NONE))
            return;
        if (command(MYBOT_UI_SET_PAUSED, !s_view.content.paused) < 0)
            return;
        mark_pending();
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
        mybot_ui_port_event(MYBOT_KEY_EVENT_CONVERSATION_STOP);
        break;
    default:
        return;
    }
    redraw();
}
} // namespace

int mybot_lvgl_view_create(lv_display_t *display) {
    if (!display || s_view.root || lv_display_get_horizontal_resolution(display) != kDisplayWidth ||
        lv_display_get_vertical_resolution(display) != kDisplayHeight)
        return -1;
    s_view.content.screen = MYBOT_LCD_SCREEN_STARTING;
    s_view.content.volume = -1;
    s_view.root = panel(lv_display_get_screen_active(display), 0, 0, kDisplayWidth, kDisplayHeight,
                        kBackground, 0);
    /* The Mosaico cover glass masks the panel corners. Keep the status strip
     * inside the straight top edge; body controls also leave a wider margin. */
    const int status_inset = 60;
    s_view.network =
        label(s_view.root, status_inset, 16, 240, 30, "", &mybot_lvgl_font_20, LV_TEXT_ALIGN_LEFT);
    const int clock_width = 100;
    s_view.clock = label(s_view.root, kDisplayWidth - status_inset - clock_width, 16, clock_width,
                         30, "--:--", &mybot_lvgl_font_20, LV_TEXT_ALIGN_RIGHT);
    s_view.body = panel(s_view.root, 0, 52, kDisplayWidth, kDisplayHeight - 52, kBackground, 0);
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
    const bool was_recovery_confirmation = s_view.page == Page::StartRecoveryConfirm;
    const bool recovery_pending = was_recovery_confirmation && s_view.command_pending;
    const int previous_pending_mode = s_view.pending_mode;
    const int previous_pending_direction = s_view.pending_direction;
    s_view.content = *content;
    const bool selection_finished =
        content->selection_revision != s_view.pending_selection_revision;
    const bool recovery_acknowledged = recovery_pending && selection_finished;
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
    if (!captions_only && (!recovery_pending || changed || recovery_acknowledged))
        s_view.command_pending = false;
    if (changed || (s_view.page == Page::Service && completed_cleanup_hint())) {
        if (content->screen == MYBOT_LCD_SCREEN_READY) {
            s_view.page =
                was_recovery_confirmation ? Page::Home
                : s_view.stop_requested
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
    if (was_recovery_confirmation && content->screen == MYBOT_LCD_SCREEN_TRANSLATION_ERROR)
        s_view.page = Page::StartRecoveryConfirm;
    s_view.start_requested = false;
    if (changed || language_changed || paused_changed || detail_changed || mode_changed ||
        capability_changed || selection_acknowledged || recovery_acknowledged ||
        (direction_changed && s_view.page == Page::Live) || (was_pending && !captions_only) ||
        (s_view.page == Page::Setup || s_view.page == Page::Service))
        redraw();
    else {
        update_volume();
        update_clock(nullptr);
    }
    if (recovery_acknowledged && start_recovery_available())
        caption_text(s_view.caption_notice,
                     tr("恢复未完成，请重试或重启设备",
                        "Recovery incomplete. Retry or restart the device."));
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
