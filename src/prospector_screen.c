/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * ZMK custom status screen for the prospector shield: the battery of each
 * keyboard half, left half in the bottom-left corner, right half in the
 * bottom-right. The space above is kept for the sprite (canon task t-rx4e).
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

LOG_MODULE_REGISTER(beacon_screen, LOG_LEVEL_INF);

/* The broadcaster answers every scan request (ZMK advertises every 100-150 ms);
 * its known payload-free windows are about 2 s after its boot and, after a
 * failed connection attempt restarts ZMK's advertising, up to its 30 s idle
 * update period. */
#define STALE_AFTER_MS 60000
#define REFRESH_MS 500
#define MARGIN_PX 12
/* Logging builds print the screen state on every change and at least this
 * often, so a long run's log shows the display thread alive. */
#define LOG_EVERY_MS 60000

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

lv_obj_t *zmk_display_status_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    make_label(&left_side, screen, LV_ALIGN_BOTTOM_LEFT, MARGIN_PX);
    make_label(&right_side, screen, LV_ALIGN_BOTTOM_RIGHT, -MARGIN_PX);

    lv_timer_create(refresh, REFRESH_MS, NULL);
    return screen;
}
