/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * A battery readings style: what the status screen shows of the halves'
 * batteries under the sprite. The BEACON_READINGS Kconfig choice picks one
 * implementation (readings_digits.c, hp_bar.c); prospector_screen.c only
 * places it and feeds it the observer's state. Display work queue only: LVGL
 * is not thread-safe here (LV_USE_OS=0).
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <lvgl.h>

#include "status_observer.h"

/* The readings' distance from the panel's edges. The panel's corners are
 * rounded: a box closer to them loses its own corners (hardware 2026-09-28). */
#define BEACON_READINGS_MARGIN_PX 12

struct beacon_readings {
    /* The first panel row the readings can ink; a sprite above them ends
     * there. */
    int32_t (*top)(int32_t screen_h);
    /* Creates the widgets in parent, showing no reading. Called once. */
    void (*create)(lv_obj_t *parent, int32_t screen_w);
    /* Shows the observer's state; fresh is whether a payload arrived recently
     * enough to trust it. Returns whether anything shown changed. */
    bool (*show)(const struct beacon_status *now, bool fresh);
};

extern const struct beacon_readings beacon_readings_digits;
extern const struct beacon_readings beacon_readings_hp_bar;
