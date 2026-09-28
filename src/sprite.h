/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * The status screen's GIF sprite (CONFIG_BEACON_SPRITE). Display work queue
 * only: LVGL is not thread-safe here (LV_USE_OS=0).
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <lvgl.h>

/* Creates the sprite inside parent, in box (parent coordinates), playing at
 * speed_pct of the GIF's own tempo (100 = its timing, 200 twice as fast; not
 * 0). fill false: scaled by the largest whole factor that fits, top-centred.
 * fill true: the largest size of the GIF's proportions that fits, whole
 * factor or not, centred. NULL: the GIF could not be opened or does not fit
 * at 1x, and the screen goes on without a sprite. Call once. */
lv_obj_t *beacon_sprite_create(lv_obj_t *parent, const lv_area_t *box, uint16_t speed_pct,
                               bool fill);
