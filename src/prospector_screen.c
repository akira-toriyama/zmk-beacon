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
 * edges. BEACON_READINGS_NONE gives the sprite the whole panel and shows the
 * digits only when there is no sprite: the build has none (CI, the release
 * build), or it stopped on malformed GIF data, which its object's
 * LV_EVENT_DELETE reports.
 *
 * The sprite must stay the screen's first child and nothing drawn before it
 * may render through a layer: sprite.c's blit() writes into the display
 * buffer during its own draw event and relies on every earlier draw task
 * having landed (the conditions are in sprite.c).
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
/* Sprite tempo while nobody types, in percent of the GIF's own: the user's
 * pick (2026-09-29, after 100 and 50 the same day; 150 with no keyboard link
 * from 09-28). Key presses step the sprite frame by frame instead (sprite.c),
 * and the player keeps the tempo by merging frames when drawing falls behind. */
#define SPRITE_SPEED_PCT 75

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
        LOG_INF("screen left %u right %u (%s, payload %d ms ago, %u keystrokes)", now.left,
                now.right, fresh ? "fresh" : "stale", now.received ? (int)MIN(age_ms, INT32_MAX) : -1,
                now.keystrokes);
    }
}

static void show_readings(lv_obj_t *screen) {
    READINGS->create(screen, lv_display_get_horizontal_resolution(NULL));
    lv_timer_create(refresh, REFRESH_MS, NULL);
}

#if IS_ENABLED(CONFIG_BEACON_SPRITE) && IS_ENABLED(CONFIG_BEACON_READINGS_NONE)
/* The sprite removed itself (sprite.c); the screen gets the digits after all.
 * Called from the sprite's timer, so LVGL objects may be created here. */
static void sprite_gone(lv_event_t *e) {
    show_readings(lv_obj_get_parent(lv_event_get_target(e)));
}
#endif

lv_obj_t *zmk_display_status_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

#if IS_ENABLED(CONFIG_BEACON_SPRITE)
    /* The screen's first child, see above. */
    const bool fill = IS_ENABLED(CONFIG_BEACON_SPRITE_FILL);
    const int32_t inset = fill ? SPRITE_FILL_MARGIN_PX : 0;
    const int32_t screen_h = lv_display_get_vertical_resolution(NULL);
    const lv_area_t box = {
        .x1 = inset,
        .y1 = inset,
        .x2 = lv_display_get_horizontal_resolution(NULL) - 1 - inset,
        .y2 = (IS_ENABLED(CONFIG_BEACON_READINGS_NONE) ? screen_h - inset
                                                       : READINGS->top(screen_h)) -
              1,
    };
#if IS_ENABLED(CONFIG_BEACON_READINGS_NONE)
    lv_obj_t *sprite = beacon_sprite_create(screen, &box, SPRITE_SPEED_PCT, fill);
    if (sprite != NULL) {
        lv_obj_add_event_cb(sprite, sprite_gone, LV_EVENT_DELETE, NULL);
        return screen;
    }
#else
    beacon_sprite_create(screen, &box, SPRITE_SPEED_PCT, fill);
#endif
#endif

    show_readings(screen);
    return screen;
}
