/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * The digits readings (BEACON_READINGS_DIGITS, the default): each half's
 * battery in Montserrat 48, the left half in the bottom-left corner, the
 * right half in the bottom-right.
 *
 *   "75%"  white: a reading from a recent payload
 *   "--"   white: the payload is recent but that half has no reading
 *   "--"   grey:  no recent payload, or none since boot
 */

#include <stdio.h>
#include <string.h>

#include <lvgl.h>
#include <zephyr/sys/util.h>

#include "readings.h"

/* Every character a label can show. */
#define LABEL_CHARS "0123456789%-"

struct side {
    lv_obj_t *label;
    char text[8];
    bool dim;
};

static struct side left_side;
static struct side right_side;

static lv_color_t text_color(bool dim) {
    return dim ? lv_color_hex(0x606060) : lv_color_white();
}

static void make_label(struct side *side, lv_obj_t *parent, lv_align_t align, int32_t x) {
    side->label = lv_label_create(parent);
    side->dim = true;
    strcpy(side->text, "--");
    lv_obj_set_style_text_font(side->label, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(side->label, text_color(side->dim), 0);
    lv_label_set_text(side->label, side->text);
    lv_obj_align(side->label, align, x, -BEACON_READINGS_MARGIN_PX);
}

static bool show_side(struct side *side, bool fresh, uint8_t level) {
    char text[sizeof(side->text)];

    if (fresh && level > 0 && level <= 100) {
        snprintf(text, sizeof(text), "%u%%", level);
    } else {
        strcpy(text, "--");
    }

    bool changed = false;
    if (strcmp(text, side->text) != 0) {
        strcpy(side->text, text);
        lv_label_set_text(side->label, text);
        changed = true;
    }
    if (side->dim != !fresh) {
        side->dim = !fresh;
        lv_obj_set_style_text_color(side->label, text_color(side->dim), 0);
        changed = true;
    }
    return changed;
}

/* lv_draw_label puts a glyph's top at label_top + (line_height - base_line) -
 * box_h - ofs_y, and the labels sit line_height + margin above the bottom
 * edge. */
static int32_t top(int32_t screen_h) {
    const lv_font_t *font = &lv_font_montserrat_48;
    int32_t ink_above_baseline = 0;

    for (const char *c = LABEL_CHARS; *c != '\0'; c++) {
        lv_font_glyph_dsc_t g;
        if (lv_font_get_glyph_dsc(font, &g, (uint32_t)(unsigned char)*c, 0)) {
            ink_above_baseline = MAX(ink_above_baseline, g.box_h + g.ofs_y);
        }
    }
    return screen_h - BEACON_READINGS_MARGIN_PX - font->base_line - ink_above_baseline;
}

static void create(lv_obj_t *parent, int32_t screen_w) {
    ARG_UNUSED(screen_w);
    make_label(&left_side, parent, LV_ALIGN_BOTTOM_LEFT, BEACON_READINGS_MARGIN_PX);
    make_label(&right_side, parent, LV_ALIGN_BOTTOM_RIGHT, -BEACON_READINGS_MARGIN_PX);
}

static bool show(const struct beacon_status *now, bool fresh) {
    const bool left = show_side(&left_side, fresh, now->left);
    const bool right = show_side(&right_side, fresh, now->right);
    return left || right;
}

const struct beacon_readings beacon_readings_digits = {
    .top = top,
    .create = create,
    .show = show,
};
