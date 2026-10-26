/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * The HP bar (hp_bar.h): one battle-screen HP bar in a dark box along the
 * bottom: "HP", the bar with its value ("65/100") on it and, with a sprite
 * name (CONFIG_BEACON_SPRITE_NAME), the name on the row above; the layout
 * the user picked from twelve trials (canon t-er81), the name moved above
 * the bar on request (2026-10-26). The bar's length and the
 * value are the keyboard's battery as hp_level() maps it. Green, yellow under
 * YELLOW_BELOW, red under RED_BELOW; without a reading an empty track with
 * "--/100" on it, and a grey "HP", value and name without a fresh payload.
 *
 * "HP", the value and an ASCII name are unscii 16: a 16 px cell on a 17 px
 * line (base line 0). Capitals and digits ink rows y+1..y+14 of a label at y
 * (most 12 px wide, K M N W 4 are 14, X Y and "/" the full 16); a descender
 * (g, j, p, q, y) and "," ";" "_" reach row y+16. A katakana name is the
 * pkmn font (font_pkmn.h): a 16 px cell, its strokes 3 px (a pixel bolder
 * than the dots), on a 23 px line whose top 6 px are the dakuten rows, the
 * base line 3 px above the bottom, so a label at y inks rows y..y+22 at most
 * and its cell is rows y+6..y+22. A name is one or the other, never mixed
 * (tools/sprite_pack.py check_name), so one byte of 0x80 or above picks the
 * font; the label's position is the same for both.
 * "HP" and the bar share ROW_Y (an 8 px gap read as touching on hardware,
 * 2026-09-28); the value is white, centred on the bar, its digits' ink one
 * row above and below the bar. The name is the box's first row (the reason
 * BEACON_HP_BAR_BOX_H depends on it): a katakana label at NAME_Y, its
 * dakuten from row 5 and its cell at rows 11..27; an ASCII label at
 * ASCII_NAME_Y, its cell level with the katakana one at rows 11..26 (ink
 * 12..25); the HP row's ink at rows 31..44, three rows under the katakana
 * ink. No LVGL theme is installed (LV_USE_THEME_* off), so every style is
 * set here.
 */

#include <string.h>

#include <lvgl.h>
#include <zephyr/sys/util.h>

#include "hp_bar.h"

#define BORDER_PX 2
#define ROW_Y BEACON_HP_BAR_ROW_Y
#define NAME_Y BEACON_HP_BAR_NAME_Y
#define ASCII_NAME_Y (NAME_Y + BEACON_FONT_PKMN_ABOVE_CELL)
#define GLYPH_W 16
#define GLYPH_H 16
#define LABEL_X 8
#define BAR_X 48
#define BAR_H 12
#define BAR_RIGHT_PAD 10
/* "100/100" is the widest value. */
#define VALUE_CHARS 7
#define VALUE_W (VALUE_CHARS * GLYPH_W)
/* The name's bytes; as unscii cells from LABEL_X they stay inside the 252 px
 * content width (a pack's name is never longer, but a longer text would be
 * cut after them, off the border's corner). */
#define NAME_BYTES BEACON_HP_BAR_NAME_BYTES
#define YELLOW_BELOW 50
#define RED_BELOW 20

static struct {
    lv_obj_t *label;
    lv_obj_t *bar;
    lv_obj_t *value;
    /* NULL without the name row. */
    lv_obj_t *name;
    char value_text[VALUE_CHARS + 1];
    char name_text[NAME_BYTES + 1];
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

static lv_color_t value_color(bool dim) {
    return dim ? lv_color_hex(0x909090) : lv_color_white();
}

static lv_color_t name_color(bool dim) {
    return dim ? lv_color_hex(0x606060) : lv_color_hex(0xE0E0E0);
}

#if BEACON_HP_BAR_NAME_ROW
/* The font is linked only with the row (CMakeLists.txt), hence the #if. */
static const lv_font_t *name_font(const char *name, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if ((unsigned char)name[i] >= 0x80) {
            return &beacon_font_pkmn;
        }
    }
    return &lv_font_unscii_16;
}
#endif

static void set_value(uint8_t level) {
    if (level == 0) {
        lv_snprintf(hp.value_text, sizeof(hp.value_text), "--/100");
    } else {
        lv_snprintf(hp.value_text, sizeof(hp.value_text), "%u/100", level);
    }
    lv_label_set_text_static(hp.value, hp.value_text);
}

static lv_obj_t *make_box(lv_obj_t *parent, int32_t width) {
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(box, width, BEACON_HP_BAR_BOX_H);
    lv_obj_align(box, LV_ALIGN_BOTTOM_MID, 0, -BEACON_HP_BAR_MARGIN_PX);
    lv_obj_set_style_bg_color(box, lv_color_hex(0x282828), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(box, lv_color_hex(0xE0E0E0), 0);
    lv_obj_set_style_border_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(box, BORDER_PX, 0);
    lv_obj_set_style_radius(box, 8, 0);
    lv_obj_set_style_pad_all(box, 0, 0);
    return box;
}

/* Positions are inside the box's border (pad 0). */
void beacon_hp_bar_create(lv_obj_t *parent, int32_t screen_w) {
    const int32_t width = screen_w - 2 * BEACON_HP_BAR_MARGIN_PX;
    const int32_t bar_w = width - 2 * BORDER_PX - BAR_X - BAR_RIGHT_PAD;
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
    lv_obj_set_size(hp.bar, bar_w, BAR_H);
    lv_obj_set_pos(hp.bar, BAR_X, ROW_Y + (GLYPH_H - BAR_H) / 2);
    lv_obj_set_style_bg_color(hp.bar, lv_color_hex(0x404848), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(hp.bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(hp.bar, BAR_H / 2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(hp.bar, bar_color(0), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(hp.bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(hp.bar, BAR_H / 2, LV_PART_INDICATOR);

    /* Created after the bar, so it draws over it. */
    hp.value = lv_label_create(box);
    lv_obj_set_style_text_font(hp.value, &lv_font_unscii_16, 0);
    lv_obj_set_style_text_color(hp.value, value_color(hp.dim), 0);
    lv_obj_set_style_text_align(hp.value, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(hp.value, VALUE_W);
    lv_obj_set_pos(hp.value, BAR_X + (bar_w - VALUE_W) / 2, ROW_Y);
    set_value(0);

    hp.name = NULL;
    if (BEACON_HP_BAR_NAME_ROW) {
        /* One row of NAME_BYTES cells; the user accepted that a longer
         * name is cut (2026-09-28). Empty until a pack names itself. */
        hp.name = lv_label_create(box);
        lv_obj_set_style_text_font(hp.name, &lv_font_unscii_16, 0);
        lv_obj_set_style_text_color(hp.name, name_color(hp.dim), 0);
        lv_label_set_long_mode(hp.name, LV_LABEL_LONG_MODE_CLIP);
        lv_obj_set_width(hp.name, NAME_BYTES * GLYPH_W);
        lv_label_set_text_static(hp.name, hp.name_text);
        lv_obj_set_pos(hp.name, LABEL_X, ASCII_NAME_Y);
    }
}

void beacon_hp_bar_set_name(const char *name, size_t len) {
#if BEACON_HP_BAR_NAME_ROW
    if (hp.name == NULL) {
        return;
    }
    len = MIN(len, (size_t)NAME_BYTES);
    memcpy(hp.name_text, name, len);
    hp.name_text[len] = '\0';
    const lv_font_t *font = name_font(hp.name_text, len);

    lv_obj_set_style_text_font(hp.name, font, 0);
    lv_obj_set_y(hp.name, font == &beacon_font_pkmn ? NAME_Y : ASCII_NAME_Y);
    /* Set again: the label measures the text when it is set. */
    lv_label_set_text_static(hp.name, hp.name_text);
#else
    ARG_UNUSED(name);
    ARG_UNUSED(len);
#endif
}

bool beacon_hp_bar_show(const struct beacon_status *now, bool fresh) {
    const uint8_t level = hp_level(now, fresh);
    bool changed = false;

    if (level != hp.shown) {
        hp.shown = level;
        lv_bar_set_value(hp.bar, level, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(hp.bar, bar_color(level), LV_PART_INDICATOR);
        set_value(level);
        changed = true;
    }
    if (hp.dim != !fresh) {
        hp.dim = !fresh;
        lv_obj_set_style_text_color(hp.label, label_color(hp.dim), 0);
        lv_obj_set_style_text_color(hp.value, value_color(hp.dim), 0);
        if (hp.name != NULL) {
            lv_obj_set_style_text_color(hp.name, name_color(hp.dim), 0);
        }
        changed = true;
    }
    return changed;
}
