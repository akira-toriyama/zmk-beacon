/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * ZMK custom status screen for the prospector shield: the battery readings
 * (readings.h; the BEACON_READINGS choice picks the digits or an HP bar)
 * along the bottom and, with CONFIG_BEACON_SPRITE, the GIF sprite (sprite.c)
 * above them, never over them. Two siblings: what the readings show and what
 * plays above them change independently.
 *
 * The sprite is scaled by a whole factor and top-centred, flush with the
 * panel's edges (the default), or with CONFIG_BEACON_SPRITE_FILL the largest
 * size of its proportions that fits between the top edge and the readings,
 * whole factor or not, standing on them, SPRITE_FILL_MARGIN_PX in from the
 * edges (the user's v1, 2026-09-28). BEACON_READINGS_NONE leaves the readings
 * out while a sprite shows and falls back to the digits without one, so a
 * consumer's conf stays valid for CI and the release build.
 *
 * LVGL is not thread-safe here (LV_USE_OS=0) and runs on ZMK's display work
 * queue, so the readings are refreshed from an lv_timer, which runs on that
 * queue, reading the observer's state through beacon_status_get(). No LVGL
 * theme is installed (LV_USE_THEME_* off): every style is set by the file
 * that owns the widget.
 */

#include <lvgl.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zmk/display/status_screen.h>

#include "readings.h"
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
#define SPRITE_FILL_MARGIN_PX 2
/* Logging builds print the screen state on every change and at least this
 * often, so a long run's log shows the display thread alive. */
#define LOG_EVERY_MS 60000
/* Sprite tempo in percent of the GIF's own: the user's pick (2026-09-28). The
 * player keeps it by merging frames when drawing falls behind (sprite.c). */
#define SPRITE_SPEED_PCT 150

#if IS_ENABLED(CONFIG_BEACON_READINGS_HP_BAR)
#define READINGS (&beacon_readings_hp_bar)
#else
#define READINGS (&beacon_readings_digits)
#endif

static void refresh(lv_timer_t *timer) {
    ARG_UNUSED(timer);
    static int64_t logged_ms;

    struct beacon_status now;
    beacon_status_get(&now);
    const int64_t now_ms = k_uptime_get();
    const int64_t age_ms = now_ms - now.last_ms;
    const bool fresh = now.received && age_ms <= STALE_AFTER_MS;

    const bool changed = READINGS->show(&now, fresh);

    if (IS_ENABLED(CONFIG_LOG) && (changed || now_ms - logged_ms >= LOG_EVERY_MS)) {
        logged_ms = now_ms;
        LOG_INF("screen left %u right %u (%s, payload %d ms ago)", now.left, now.right,
                fresh ? "fresh" : "stale", now.received ? (int)MIN(age_ms, INT32_MAX) : -1);
    }
}

lv_obj_t *zmk_display_status_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    const int32_t screen_w = lv_display_get_horizontal_resolution(NULL);
    bool sprite = false;

#if IS_ENABLED(CONFIG_BEACON_SPRITE)
    /* Created before the readings so that they draw on top of it if the two
     * ever meet. */
    const bool fill = IS_ENABLED(CONFIG_BEACON_SPRITE_FILL);
    const int32_t inset = fill ? SPRITE_FILL_MARGIN_PX : 0;
    const lv_area_t box = {
        .x1 = inset,
        .y1 = inset,
        .x2 = screen_w - 1 - inset,
        .y2 = READINGS->top(lv_display_get_vertical_resolution(NULL)) - 1,
    };
    sprite = beacon_sprite_create(screen, &box, SPRITE_SPEED_PCT, fill) != NULL;
#endif

    if (IS_ENABLED(CONFIG_BEACON_READINGS_NONE) && sprite) {
        return screen;
    }
    READINGS->create(screen, screen_w);
    lv_timer_create(refresh, REFRESH_MS, NULL);
    return screen;
}
