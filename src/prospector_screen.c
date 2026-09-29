/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * ZMK custom status screen for the prospector shield: the HP bar (hp_bar.h)
 * along the bottom and, with CONFIG_BEACON_SPRITE, the GIF sprite (sprite.h)
 * above it, filling the space between the panel's top edge and the bar,
 * SPRITE_INSET_PX in from the edges and standing on the bar.
 *
 * The sprite must stay the screen's first child and nothing drawn before it
 * may render through a layer: sprite.c's blit() writes into the display
 * buffer during its own draw event and relies on every earlier draw task
 * having landed (the conditions are in sprite.c).
 *
 * LVGL is not thread-safe here (LV_USE_OS=0) and runs on ZMK's display work
 * queue, so the HP bar is refreshed from an lv_timer, which runs on that
 * queue, reading the observer's state through beacon_status_get(). No LVGL
 * theme is installed (LV_USE_THEME_* off): every style is set by the file
 * that owns the widget.
 */

#include <lvgl.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <zmk/display/status_screen.h>

#include "hp_bar.h"
#include "status_observer.h"
#if IS_ENABLED(CONFIG_BEACON_SPRITE)
#include "sprite.h"
#endif

LOG_MODULE_REGISTER(beacon_screen, LOG_LEVEL_INF);

/* A payload older than this greys the HP bar. The broadcaster sends one every
 * BEACON_PAYLOAD_INTERVAL_MS, so lost ones never come near it; a minute also
 * rides out a reboot or reflash of the Imprint Dongle without greying the
 * screen, and the battery levels shown change far slower than that. */
#define STALE_AFTER_MS 60000
#define REFRESH_MS 500
/* The panel as LVGL sees it: the Zephyr LVGL glue sizes its display from the
 * driver's capabilities, which the ST7789V driver takes from these. */
#define SCREEN_W DT_PROP(DT_CHOSEN(zephyr_display), width)
#define SCREEN_H DT_PROP(DT_CHOSEN(zephyr_display), height)
/* The sprite box's distance from the panel's top and side edges. */
#define SPRITE_INSET_PX 2
#define SPRITE_BOX_W (SCREEN_W - 2 * SPRITE_INSET_PX)
#define SPRITE_BOX_H (BEACON_HP_BAR_TOP(SCREEN_H) - SPRITE_INSET_PX)
/* Logging builds print the screen state on every change and at least this
 * often, so a long run's log shows the display thread alive. */
#define LOG_EVERY_MS 60000

#if IS_ENABLED(CONFIG_BEACON_SPRITE)
/* CMakeLists.txt passes the GIF's size; sprite.c scales it up, never down.
 * The messages avoid apostrophes, which GCC prints escaped. */
BUILD_ASSERT(BEACON_SPRITE_GIF_W <= SPRITE_BOX_W,
             "CONFIG_BEACON_SPRITE_GIF is wider than the sprite box: the panel width less "
             "SPRITE_INSET_PX on each side (prospector_screen.c)");
BUILD_ASSERT(BEACON_SPRITE_GIF_H <= SPRITE_BOX_H,
             "CONFIG_BEACON_SPRITE_GIF is taller than the sprite box: the panel height less "
             "SPRITE_INSET_PX above and the HP bar box and margin below, a row taller with "
             "CONFIG_BEACON_SPRITE_NAME (prospector_screen.c, hp_bar.h)");
#endif

static void refresh(lv_timer_t *timer) {
    ARG_UNUSED(timer);
    static int64_t logged_ms;

    struct beacon_status now;
    beacon_status_get(&now);
    const int64_t now_ms = k_uptime_get();
    const int64_t age_ms = now_ms - now.last_ms;
    const bool fresh = now.received && age_ms <= STALE_AFTER_MS;

    const bool changed = beacon_hp_bar_show(&now, fresh);

    if (IS_ENABLED(CONFIG_LOG) && (changed || now_ms - logged_ms >= LOG_EVERY_MS)) {
        logged_ms = now_ms;
        LOG_INF("screen left %u right %u (%s, payload %d ms ago, %u keystrokes)", now.left,
                now.right, fresh ? "fresh" : "stale", now.received ? (int)MIN(age_ms, INT32_MAX) : -1,
                now.keystrokes);
    }
}

lv_obj_t *zmk_display_status_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);

    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

#if IS_ENABLED(CONFIG_BEACON_SPRITE)
    /* The screen's first child, see above. */
    const lv_area_t box = {
        .x1 = SPRITE_INSET_PX,
        .y1 = SPRITE_INSET_PX,
        .x2 = SPRITE_INSET_PX + SPRITE_BOX_W - 1,
        .y2 = SPRITE_INSET_PX + SPRITE_BOX_H - 1,
    };
    beacon_sprite_create(screen, &box);
#endif

    beacon_hp_bar_create(screen, SCREEN_W);
    lv_timer_create(refresh, REFRESH_MS, NULL);
    return screen;
}
