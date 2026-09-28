/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * The HP bar readings (BEACON_READINGS_HP_BAR): one battle-screen HP bar,
 * "HP" and a bar in a dark box along the bottom, after the games. Its length
 * is the keyboard's battery as hp_level() maps it: the mean of the halves
 * with a reading, or the one half that has one (the user's pick, 2026-09-28).
 * Green, yellow under YELLOW_BELOW, red under RED_BELOW; the digits' "--" is
 * an empty track, and their grey is a grey "HP". One row of the 8x16 unscii
 * glyph: "HP", a gap (8 px read as touching on hardware, 2026-09-28), then the
 * bar to the right edge. No LVGL theme is installed (LV_USE_THEME_* off), so
 * every style is set here.
 */

#include <lvgl.h>

#include "readings.h"

#define BOX_H 28
#define BORDER_PX 2
#define ROW_Y 6
#define GLYPH_H 16
#define LABEL_X 8
#define BAR_X 48
#define BAR_H 12
#define BAR_RIGHT_PAD 10
#define YELLOW_BELOW 50
#define RED_BELOW 20

static struct {
    lv_obj_t *label;
    lv_obj_t *bar;
    /* 0 = no reading. */
    uint8_t shown;
    bool dim;
} hp;

static bool has_reading(uint8_t level) {
    return level > 0 && level <= 100;
}

static uint8_t hp_level(const struct beacon_status *now, bool fresh) {
    const bool left = fresh && has_reading(now->left);
    const bool right = fresh && has_reading(now->right);

    if (left && right) {
        return (now->left + now->right + 1) / 2;
    }
    return left ? now->left : right ? now->right : 0;
}

static lv_color_t bar_color(uint8_t level) {
    if (level < RED_BELOW) {
        return lv_color_hex(0xF85838);
    }
    if (level < YELLOW_BELOW) {
        return lv_color_hex(0xF8E038);
    }
    return lv_color_hex(0x58D080);
}

static lv_color_t label_color(bool dim) {
    return dim ? lv_color_hex(0x606060) : lv_color_hex(0xF8C048);
}

static lv_obj_t *make_box(lv_obj_t *parent, int32_t width) {
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(box, width, BOX_H);
    lv_obj_align(box, LV_ALIGN_BOTTOM_MID, 0, -BEACON_READINGS_MARGIN_PX);
    lv_obj_set_style_bg_color(box, lv_color_hex(0x282828), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(box, lv_color_hex(0xE0E0E0), 0);
    lv_obj_set_style_border_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(box, BORDER_PX, 0);
    lv_obj_set_style_radius(box, 8, 0);
    lv_obj_set_style_pad_all(box, 0, 0);
    return box;
}

static int32_t top(int32_t screen_h) {
    return screen_h - BEACON_READINGS_MARGIN_PX - BOX_H;
}

/* Positions are inside the box's border (pad 0). */
static void create(lv_obj_t *parent, int32_t screen_w) {
    const int32_t width = screen_w - 2 * BEACON_READINGS_MARGIN_PX;
    lv_obj_t *box = make_box(parent, width);

    hp.dim = true;
    hp.shown = 0;
    hp.label = lv_label_create(box);
    lv_obj_set_style_text_font(hp.label, &lv_font_unscii_16, 0);
    lv_obj_set_style_text_color(hp.label, label_color(hp.dim), 0);
    lv_label_set_text_static(hp.label, "HP");
    lv_obj_set_pos(hp.label, LABEL_X, ROW_Y);

    hp.bar = lv_bar_create(box);
    lv_bar_set_range(hp.bar, 0, 100);
    lv_bar_set_value(hp.bar, 0, LV_ANIM_OFF);
    lv_obj_set_size(hp.bar, width - 2 * BORDER_PX - BAR_X - BAR_RIGHT_PAD, BAR_H);
    lv_obj_set_pos(hp.bar, BAR_X, ROW_Y + (GLYPH_H - BAR_H) / 2);
    lv_obj_set_style_bg_color(hp.bar, lv_color_hex(0x404848), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(hp.bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(hp.bar, BAR_H / 2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(hp.bar, bar_color(0), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(hp.bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(hp.bar, BAR_H / 2, LV_PART_INDICATOR);
}

static bool show(const struct beacon_status *now, bool fresh) {
    const uint8_t level = hp_level(now, fresh);
    bool changed = false;

    if (level != hp.shown) {
        hp.shown = level;
        lv_bar_set_value(hp.bar, level, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(hp.bar, bar_color(level), LV_PART_INDICATOR);
        changed = true;
    }
    if (hp.dim != !fresh) {
        hp.dim = !fresh;
        lv_obj_set_style_text_color(hp.label, label_color(hp.dim), 0);
        changed = true;
    }
    return changed;
}

const struct beacon_readings beacon_readings_hp_bar = {
    .top = top,
    .create = create,
    .show = show,
};
