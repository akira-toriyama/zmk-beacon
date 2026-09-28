/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * ZMK custom status screen for the prospector shield: the battery of each
 * keyboard half, left half in the bottom-left corner, right half in the
 * bottom-right, and with CONFIG_BEACON_SPRITE the GIF sprite in the space
 * above them (sprite.c).
 *
 *   "75%"  white: a reading from a payload received in the last minute
 *   "--"   white: the payload is current but that half has no reading
 *   "--"   grey:  no payload in the last minute, or none since boot
 *
 * LVGL is not thread-safe here (LV_USE_OS=0) and runs on ZMK's display work
 * queue, so the labels are refreshed from an lv_timer, which runs on that
 * queue, reading the observer's state through beacon_status_get().
 */

#include <stdio.h>
#include <string.h>

#include <lvgl.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zmk/display/status_screen.h>

#include "status_observer.h"
#if IS_ENABLED(CONFIG_BEACON_SPRITE)
#include "sprite.h"
#endif

LOG_MODULE_REGISTER(beacon_screen, LOG_LEVEL_INF);

/* zmk-beacon's broadcaster sends a payload every 200 ms in canon (255-273 a
 * minute received on hardware, 2026-09-27). A keyboard still on
 * prospector-zmk-module v2.2.3, which the observer also accepts, carries it in
 * scan responses only, with payload-free windows of about 2 s after its boot
 * and up to its 30 s idle update period after a failed connection restarts
 * ZMK's advertising. A minute covers both. */
#define STALE_AFTER_MS 60000
#define REFRESH_MS 500
#define MARGIN_PX 12
/* Logging builds print the screen state on every change and at least this
 * often, so a long run's log shows the display thread alive. */
#define LOG_EVERY_MS 60000

#if IS_ENABLED(CONFIG_BEACON_SPRITE)
/* Sprite tempo in percent of the GIF's own: always at least SPEED_BASE (the
 * user wants it moving, keyboard heard or not; 2026-09-28), plus SPEED_PER_WPM
 * per WPM (50 WPM, an everyday pace, doubles it), capped at SPEED_MAX
 * (100 WPM; beyond that the decoder only outruns the draw). ZMK's WPM counts
 * keycode releases only (&vkey and layer keys do not count) and reads 0 1-6 s
 * after the last one, which brings the sprite back to SPEED_BASE. A WPM from
 * a payload older than WPM_MAX_AGE_MS counts as 0: the observer keeps the last
 * value when payloads stop. The age assumes the broadcaster's
 * BEACON_STATUS_BROADCAST_INTERVAL_MS (200 ms in canon) stays well under it;
 * near or above it the sprite would pulse between fast and SPEED_BASE. */
#define SPRITE_SPEED_BASE_PCT 100
#define SPRITE_SPEED_PER_WPM_PCT 2
#define SPRITE_SPEED_MAX_PCT 300
#define SPRITE_WPM_MAX_AGE_MS 5000
/* Every character a label can show; the sprite box ends where their ink begins. */
#define LABEL_CHARS "0123456789%-"
#endif

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

static bool show(struct side *side, bool fresh, uint8_t level) {
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

static void refresh(lv_timer_t *timer) {
    ARG_UNUSED(timer);
    static int64_t logged_ms;

    struct beacon_status now;
    beacon_status_get(&now);
    const int64_t now_ms = k_uptime_get();
    const int64_t age_ms = now_ms - now.last_ms;
    const bool fresh = now.received && age_ms <= STALE_AFTER_MS;

    const bool left_changed = show(&left_side, fresh, now.left);
    const bool right_changed = show(&right_side, fresh, now.right);

#if IS_ENABLED(CONFIG_BEACON_SPRITE)
    const unsigned int wpm = now.received && age_ms <= SPRITE_WPM_MAX_AGE_MS ? now.wpm : 0;

    beacon_sprite_set_speed(
        MIN(SPRITE_SPEED_BASE_PCT + SPRITE_SPEED_PER_WPM_PCT * wpm, SPRITE_SPEED_MAX_PCT));
#endif

    if (IS_ENABLED(CONFIG_LOG) &&
        (left_changed || right_changed || now_ms - logged_ms >= LOG_EVERY_MS)) {
        logged_ms = now_ms;
        LOG_INF("screen %s %s (%s, payload %d ms ago)", left_side.text, right_side.text,
                fresh ? "fresh" : "stale", now.received ? (int)MIN(age_ms, INT32_MAX) : -1);
    }
}

static void make_label(struct side *side, lv_obj_t *parent, lv_align_t align, int32_t x) {
    side->label = lv_label_create(parent);
    side->dim = true;
    strcpy(side->text, "--");
    lv_obj_set_style_text_font(side->label, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(side->label, text_color(side->dim), 0);
    lv_label_set_text(side->label, side->text);
    lv_obj_align(side->label, align, x, -MARGIN_PX);
}

#if IS_ENABLED(CONFIG_BEACON_SPRITE)
/* The first row a label can ink. lv_draw_label puts a glyph's top at
 * label_top + (line_height - base_line) - box_h - ofs_y, and the labels sit
 * line_height + MARGIN_PX above the bottom edge. */
static int32_t label_ink_top(int32_t screen_h) {
    const lv_font_t *font = &lv_font_montserrat_48;
    int32_t ink_above_baseline = 0;

    for (const char *c = LABEL_CHARS; *c != '\0'; c++) {
        lv_font_glyph_dsc_t g;
        if (lv_font_get_glyph_dsc(font, &g, (uint32_t)(unsigned char)*c, 0)) {
            ink_above_baseline = MAX(ink_above_baseline, g.box_h + g.ofs_y);
        }
    }
    return screen_h - MARGIN_PX - font->base_line - ink_above_baseline;
}
#endif

lv_obj_t *zmk_display_status_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

#if IS_ENABLED(CONFIG_BEACON_SPRITE)
    /* Created before the labels so that they draw on top of it. */
    const lv_area_t box = {
        .x1 = 0,
        .y1 = 0,
        .x2 = lv_display_get_horizontal_resolution(NULL) - 1,
        .y2 = label_ink_top(lv_display_get_vertical_resolution(NULL)) - 1,
    };
    beacon_sprite_create(screen, &box);
#endif

    make_label(&left_side, screen, LV_ALIGN_BOTTOM_LEFT, MARGIN_PX);
    make_label(&right_side, screen, LV_ALIGN_BOTTOM_RIGHT, -MARGIN_PX);

    lv_timer_create(refresh, REFRESH_MS, NULL);
    return screen;
}
