/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * ZMK custom status screen for the prospector shield. Skeleton: the layout
 * that the finished screen keeps (both halves' battery, bottom corners) with
 * placeholder text, and no data source yet. The BLE observer that feeds it is
 * the next step (canon task t-5gxp); until then the screen proves the display
 * path (ST7789V, LVGL 9, ZMK's dedicated display work queue) builds and
 * renders.
 */

#include <lvgl.h>
#include <zmk/display/status_screen.h>

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, lv_align_t align, int32_t x,
                            int32_t y, const char *text) {
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_label_set_text(label, text);
    lv_obj_align(label, align, x, y);
    return label;
}

lv_obj_t *zmk_display_status_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    make_label(screen, &lv_font_montserrat_16, LV_ALIGN_TOP_MID, 0, 8, "beacon");
    make_label(screen, &lv_font_montserrat_28, LV_ALIGN_BOTTOM_LEFT, 12, -10, "L --");
    make_label(screen, &lv_font_montserrat_28, LV_ALIGN_BOTTOM_RIGHT, -12, -10, "R --");

    return screen;
}
