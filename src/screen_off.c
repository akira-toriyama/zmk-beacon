/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * Screen off (screen_off.h): once no key press has arrived for
 * CONFIG_BEACON_SCREEN_OFF_AFTER_S the backlight fades out over FADE_MS and
 * the panel is blanked; the next key press lights the screen within a tick of
 * its payload (longer only behind a screen dump, which holds the display work
 * queue for its length). A key press is what the observer counts (status_observer.h key_ms: any key of
 * either half, and nothing else, so a pointing device does not keep the screen
 * lit). key_ms is 0 until the first press, so until then the time since boot
 * counts: a device that hears no keyboard goes dark too.
 *
 * - Not ZMK's blank-on-idle (CONFIG_ZMK_DISPLAY_BLANK_ON_IDLE, zmk
 *   app/src/display/main.c): ZMK's activity state returns to active only on
 *   the device's own position, sensor and pointing events (app/src/activity.c),
 *   which a device without keys never raises, so the display would blank
 *   CONFIG_ZMK_IDLE_TIMEOUT after boot for good. That option also stops the
 *   display tick, which every lv_timer runs on (prospector_screen.c asserts
 *   it off).
 * - LVGL keeps running while the screen is dark. The HP bar still refreshes
 *   and the panel's frame memory takes the flushes (display blanking is the
 *   ST7789V's DISPOFF; the driver sends a write the same either way, and
 *   Zephyr's display sample likewise draws before display_blanking_off()), so
 *   the screen is right from the moment it lights and a screen dump of a dark
 *   screen shows what it would show. Only the sprite stands still (sprite.c).
 * - Off: backlight 0, then blanking on. On: blanking off, then the backlight.
 *   Neither transition lights a blanked panel. The backlight alone makes it
 *   dark.
 * - The fade follows the square of the time left: the eye is more sensitive
 *   at low luminance, so a low duty looks brighter than its share and a
 *   linear ramp looks lit until its end.
 * - Display work queue only, from an lv_timer like the screen's: the driver
 *   sends a flush as several SPI transfers (zephyr
 *   drivers/display/display_st7789v.c st7789v_write()), a blanking command
 *   from another thread could land between them, and on this queue no flush
 *   is in progress while a timer runs (no CONFIG_LV_Z_FLUSH_THREAD).
 */

#include <lvgl.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "backlight.h"
#include "screen_off.h"
#include "status_observer.h"

LOG_MODULE_REGISTER(beacon_screen_off, LOG_LEVEL_INF);

BUILD_ASSERT(!IS_ENABLED(CONFIG_LV_Z_FLUSH_THREAD),
             "screen off sends the blanking command between flushes: no flush thread");

#define OFF_AFTER_MS ((int64_t)CONFIG_BEACON_SCREEN_OFF_AFTER_S * MSEC_PER_SEC)
/* How long a key press's payload waits for the light at most (sprite.c steps
 * no frame meanwhile), and a step of the fade. */
#define TICK_MS 20
#define FADE_MS 1000
#define LIT_PERCENT CONFIG_BEACON_BACKLIGHT_BRIGHTNESS

enum screen_state {
    SCREEN_LIT,
    SCREEN_FADING,
    SCREEN_OFF,
};

static const struct device *const display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

static struct {
    enum screen_state state;
    int64_t fade_start_ms;
    /* The backlight's duty as last written; backlight.c lit it at boot. */
    uint8_t percent;
} screen = {.percent = LIT_PERCENT};

bool beacon_screen_is_off(void) {
    return screen.state == SCREEN_OFF;
}

static void set_backlight(uint8_t percent) {
    if (percent == screen.percent) {
        return;
    }

    int ret = beacon_backlight_set(percent);
    if (ret < 0) {
        LOG_ERR("backlight %u: %d", percent, ret);
        return;
    }
    screen.percent = percent;
}

static void turn_off(void) {
    set_backlight(0);

    int ret = display_blanking_on(display);
    if (ret < 0) {
        LOG_ERR("blanking on: %d", ret);
    }
    screen.state = SCREEN_OFF;
    LOG_INF("screen off: no key press for %d s", CONFIG_BEACON_SCREEN_OFF_AFTER_S);
}

static void turn_on(void) {
    if (screen.state == SCREEN_OFF) {
        int ret = display_blanking_off(display);
        if (ret < 0) {
            LOG_ERR("blanking off: %d", ret);
        }
        LOG_INF("screen on: key press");
    }
    set_backlight(LIT_PERCENT);
    screen.state = SCREEN_LIT;
}

static void tick(lv_timer_t *timer) {
    ARG_UNUSED(timer);
    struct beacon_status status;

    beacon_status_get(&status);
    const int64_t now_ms = k_uptime_get();

    if (now_ms - status.key_ms < OFF_AFTER_MS) {
        if (screen.state != SCREEN_LIT) {
            turn_on();
        }
        return;
    }

    if (screen.state == SCREEN_LIT) {
        screen.state = SCREEN_FADING;
        screen.fade_start_ms = now_ms;
    }
    if (screen.state == SCREEN_FADING) {
        const int32_t left_ms =
            FADE_MS - (int32_t)MIN(now_ms - screen.fade_start_ms, (int64_t)FADE_MS);

        if (left_ms > 0) {
            set_backlight((uint8_t)(LIT_PERCENT * left_ms * left_ms / (FADE_MS * FADE_MS)));
        } else {
            turn_off();
        }
    }
}

void beacon_screen_off_start(void) {
    lv_timer_create(tick, TICK_MS, NULL);
}
