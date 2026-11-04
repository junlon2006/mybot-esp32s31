/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stdint.h>
struct lv_obj_t;
struct lv_event_t;
struct lv_display_t;
struct lv_timer_t;
struct lv_indev_t;
struct lv_point_t {
    int32_t x, y;
};
struct lv_area_t {
    int32_t x1, y1, x2, y2;
};
struct lv_font_t {
    int unused;
};
using lv_color_t = uint32_t;
using lv_event_cb_t = void (*)(lv_event_t *);
using lv_timer_cb_t = void (*)(lv_timer_t *);
#define LV_FONT_DECLARE(name) extern const lv_font_t name
enum lv_obj_flag_t {
    LV_OBJ_FLAG_SCROLLABLE = 1U,
    LV_OBJ_FLAG_CLICKABLE = 2U,
    LV_OBJ_FLAG_PRESS_LOCK = 4U
};
#define LV_STATE_DISABLED 1U
#define LV_OPA_COVER 255
#define LV_OPA_30 76
enum lv_text_align_t { LV_TEXT_ALIGN_CENTER, LV_TEXT_ALIGN_LEFT, LV_TEXT_ALIGN_RIGHT };
#define LV_LABEL_LONG_WRAP 0
enum lv_event_code_t {
    LV_EVENT_ALL,
    LV_EVENT_CLICKED,
    LV_EVENT_RELEASED,
    LV_EVENT_PRESSED,
    LV_EVENT_PRESSING,
    LV_EVENT_PRESS_LOST,
    LV_EVENT_DELETE
};
#define LV_PART_MAIN 0
#define LV_PART_INDICATOR 1
#define LV_PART_KNOB 2
#define LV_ANIM_OFF 0
lv_obj_t *lv_obj_create(lv_obj_t *);
lv_obj_t *lv_label_create(lv_obj_t *);
void lv_obj_delete(lv_obj_t *);
void lv_obj_clean(lv_obj_t *);
void lv_obj_remove_style_all(lv_obj_t *);
void lv_obj_remove_flag(lv_obj_t *, lv_obj_flag_t);
void lv_obj_add_flag(lv_obj_t *, lv_obj_flag_t);
void lv_obj_set_pos(lv_obj_t *, int, int);
void lv_obj_set_size(lv_obj_t *, int, int);
void lv_obj_set_style_bg_color(lv_obj_t *, lv_color_t, int);
void lv_obj_set_style_bg_opa(lv_obj_t *, int, int);
void lv_obj_set_style_radius(lv_obj_t *, int, int);
void lv_obj_set_style_text_font(lv_obj_t *, const lv_font_t *, int);
void lv_obj_set_style_text_color(lv_obj_t *, lv_color_t, int);
void lv_obj_set_style_text_align(lv_obj_t *, lv_text_align_t, int);
void lv_obj_set_style_opa(lv_obj_t *, int, int);
void lv_obj_add_event_cb(lv_obj_t *, lv_event_cb_t, int, void *);
lv_obj_t *lv_event_get_current_target_obj(lv_event_t *);
void *lv_event_get_user_data(lv_event_t *);
lv_event_code_t lv_event_get_code(lv_event_t *);
lv_indev_t *lv_indev_active(void);
void lv_indev_get_point(const lv_indev_t *, lv_point_t *);
void lv_obj_get_coords(const lv_obj_t *, lv_area_t *);
void lv_obj_remove_state(lv_obj_t *, unsigned);
void lv_obj_add_state(lv_obj_t *, unsigned);
bool lv_obj_has_state(lv_obj_t *, unsigned);
void lv_label_set_long_mode(lv_obj_t *, int);
void lv_label_set_text(lv_obj_t *, const char *);
lv_color_t lv_color_hex(uint32_t);
lv_obj_t *lv_display_get_screen_active(lv_display_t *);
int lv_display_get_horizontal_resolution(lv_display_t *);
int lv_display_get_vertical_resolution(lv_display_t *);
lv_timer_t *lv_timer_create(lv_timer_cb_t, unsigned, void *);
void lv_timer_delete(lv_timer_t *);
uint32_t lv_tick_get(void);
