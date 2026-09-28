/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * ZMK custom status screen for the prospector shield: the battery of each
 * keyboard half and, with CONFIG_BEACON_SPRITE, the GIF sprite (sprite.c).
 *
 * Sprite: above the readings, never over them. Scaled by a whole factor and
 * top-centred, flush with the panel's edges (the default), or with
 * CONFIG_BEACON_SPRITE_FILL the largest size of its proportions that fits
 * between the top edge and the readings, whole factor or not, standing on
 * them, SPRITE_FILL_MARGIN_PX in from the edges (the user's v1, 2026-09-28).
 * The readings keep MARGIN_PX: the panel's corners are rounded and a box
 * closer to them loses its own corners (seen on hardware 2026-09-28).
 *
 * Readings (the BEACON_READINGS choice):
 *   digits  "75%" in the bottom corners, Montserrat 48. White: a reading from
 *           a payload received in the last minute. "--" white: the payload is
 *           current but that half has no reading. "--" grey: no payload in
 *           the last minute, or none since boot.
 *   HP bar  one battle-screen HP bar (hp_bar.c) along the bottom. Its length
 *           is the keyboard's battery as hp_level() maps it: the mean of the
 *           halves with a reading, or the one half that has one. The digits'
 *           "--" is an empty track, and their grey is a grey "HP".
 *   none    no readings while a sprite shows; the digits when the build has
 *           no sprite or the GIF is rejected, so the conf stays valid for CI.
 *
 * LVGL is not thread-safe here (LV_USE_OS=0) and runs on ZMK's display work
 * queue, so the readings are refreshed from an lv_timer, which runs on that
 * queue, reading the observer's state through beacon_status_get(). No LVGL
 * theme is installed (LV_USE_THEME_* off), so every style is set here.
 */

#include <stdio.h>
#include <string.h>

#include <lvgl.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zmk/display/status_screen.h>

#include "status_observer.h"
#if IS_ENABLED(CONFIG_BEACON_READINGS_HP_BAR)
#include "hp_bar.h"
#endif
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
/* The readings' distance from the panel's edges, and a filling sprite's. */
#define MARGIN_PX 12
#define SPRITE_FILL_MARGIN_PX 2
/* Logging builds print the screen state on every change and at least this
 * often, so a long run's log shows the display thread alive. */
#define LOG_EVERY_MS 60000

#if IS_ENABLED(CONFIG_BEACON_SPRITE)
/* Sprite tempo in percent of the GIF's own, fixed: the user's choice
 * (2026-09-28) after seeing the trade on hardware. Decoding costs about 11 ms
 * a GIF frame and a render of the 2x box about 105 ms, so a faster tempo takes
 * renders away: 7 renders a second at 100 %, 6 at 150 % (hardware 2026-09-28:
 * 1,790 frames decoded and 363 renders a minute), and 3.5 at about 2.8x with a
 * catch-up limit of 16 (sprite.c CATCHUP_MAX). No link to the keyboard. */
#define SPRITE_SPEED_PCT 150
#endif

#if !IS_ENABLED(CONFIG_BEACON_READINGS_HP_BAR)
/* Every character a digits label can show; the sprite box ends where their ink
 * begins. */
#define LABEL_CHARS "0123456789%-"
#endif

static bool has_reading(uint8_t level) {
    return level > 0 && level <= 100;
}

#if IS_ENABLED(CONFIG_BEACON_READINGS_HP_BAR)
/* What the bar shows, for the log; 0 = no reading. */
static uint8_t hp_shown;

/* The keyboard's battery as one level: the mean of the halves with a reading,
 * or the one half that has one (the user's pick, 2026-09-28). */
static uint8_t hp_level(const struct beacon_status *now, bool fresh) {
    const bool left = fresh && has_reading(now->left);
    const bool right = fresh && has_reading(now->right);

    if (left && right) {
        return (now->left + now->right + 1) / 2;
    }
    return left ? now->left : right ? now->right : 0;
}

static bool show_hp(const struct beacon_status *now, bool fresh) {
    hp_shown = hp_level(now, fresh);
    return beacon_hp_bar_show(hp_shown, fresh);
}
#else
struct side {
    lv_obj_t *label;
    /* The reading as shown ("75%" / "--"), also for the log. */
    char text[8];
    bool dim;
};

static lv_color_t text_color(bool dim) {
    return dim ? lv_color_hex(0x606060) : lv_color_white();
}

static struct side left_side;
static struct side right_side;

static bool show(struct side *side, bool fresh, uint8_t level) {
    char text[sizeof(side->text)];

    if (fresh && has_reading(level)) {
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
#endif

static void refresh(lv_timer_t *timer) {
    ARG_UNUSED(timer);
    static int64_t logged_ms;

    struct beacon_status now;
    beacon_status_get(&now);
    const int64_t now_ms = k_uptime_get();
    const int64_t age_ms = now_ms - now.last_ms;
    const bool fresh = now.received && age_ms <= STALE_AFTER_MS;

#if IS_ENABLED(CONFIG_BEACON_READINGS_HP_BAR)
    const bool changed = show_hp(&now, fresh);

    if (IS_ENABLED(CONFIG_LOG) && (changed || now_ms - logged_ms >= LOG_EVERY_MS)) {
        logged_ms = now_ms;
        LOG_INF("screen HP %u%% (left %u right %u, %s, payload %d ms ago)", hp_shown, now.left,
                now.right, fresh ? "fresh" : "stale",
                now.received ? (int)MIN(age_ms, INT32_MAX) : -1);
    }
#else
    const bool left_changed = show(&left_side, fresh, now.left);
    const bool right_changed = show(&right_side, fresh, now.right);

    if (IS_ENABLED(CONFIG_LOG) &&
        (left_changed || right_changed || now_ms - logged_ms >= LOG_EVERY_MS)) {
        logged_ms = now_ms;
        LOG_INF("screen %s %s (%s, payload %d ms ago)", left_side.text, right_side.text,
                fresh ? "fresh" : "stale", now.received ? (int)MIN(age_ms, INT32_MAX) : -1);
    }
#endif
}

#if !IS_ENABLED(CONFIG_BEACON_READINGS_HP_BAR)
static void make_digits(struct side *side, lv_obj_t *parent, lv_align_t align, int32_t x) {
    side->label = lv_label_create(parent);
    side->dim = true;
    strcpy(side->text, "--");
    lv_obj_set_style_text_font(side->label, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(side->label, text_color(side->dim), 0);
    lv_label_set_text(side->label, side->text);
    lv_obj_align(side->label, align, x, -MARGIN_PX);
}
#endif

#if IS_ENABLED(CONFIG_BEACON_SPRITE)
/* The first row the readings can ink; a sprite above them ends there. */
static int32_t readings_top(int32_t screen_h) {
#if IS_ENABLED(CONFIG_BEACON_READINGS_HP_BAR)
    return screen_h - MARGIN_PX - BEACON_HP_BAR_H;
#else
    /* lv_draw_label puts a glyph's top at label_top + (line_height -
     * base_line) - box_h - ofs_y, and the labels sit line_height + MARGIN_PX
     * above the bottom edge. */
    const lv_font_t *font = &lv_font_montserrat_48;
    int32_t ink_above_baseline = 0;

    for (const char *c = LABEL_CHARS; *c != '\0'; c++) {
        lv_font_glyph_dsc_t g;
        if (lv_font_get_glyph_dsc(font, &g, (uint32_t)(unsigned char)*c, 0)) {
            ink_above_baseline = MAX(ink_above_baseline, g.box_h + g.ofs_y);
        }
    }
    return screen_h - MARGIN_PX - font->base_line - ink_above_baseline;
#endif
}
#endif

lv_obj_t *zmk_display_status_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    const int32_t screen_w = lv_display_get_horizontal_resolution(NULL);
    bool sprite = false;

#if IS_ENABLED(CONFIG_BEACON_SPRITE)
    /* Between the top edge and the readings; filling, inside its own margin,
     * standing on the readings. */
    const bool fill = IS_ENABLED(CONFIG_BEACON_SPRITE_FILL);
    const int32_t inset = fill ? SPRITE_FILL_MARGIN_PX : 0;
    const lv_area_t box = {
        .x1 = inset,
        .y1 = inset,
        .x2 = screen_w - 1 - inset,
        .y2 = readings_top(lv_display_get_vertical_resolution(NULL)) - 1,
    };
    sprite = beacon_sprite_create(screen, &box, SPRITE_SPEED_PCT, fill) != NULL;
#endif

    if (IS_ENABLED(CONFIG_BEACON_READINGS_NONE) && sprite) {
        return screen;
    }
#if IS_ENABLED(CONFIG_BEACON_READINGS_HP_BAR)
    beacon_hp_bar_create(screen, screen_w - 2 * MARGIN_PX, MARGIN_PX);
#else
    make_digits(&left_side, screen, LV_ALIGN_BOTTOM_LEFT, MARGIN_PX);
    make_digits(&right_side, screen, LV_ALIGN_BOTTOM_RIGHT, -MARGIN_PX);
#endif
    lv_timer_create(refresh, REFRESH_MS, NULL);
    return screen;
}
