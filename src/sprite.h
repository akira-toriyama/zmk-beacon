/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * The status screen's GIF sprite (CONFIG_BEACON_SPRITE, sprite.c). Display
 * work queue only: LVGL is not thread-safe here (LV_USE_OS=0).
 */

#pragma once

#include <stdint.h>

#include <lvgl.h>

/* Creates the sprite in box (parent coordinates), which must hold the GIF at
 * 1x: scaled to the largest size of the GIF's proportions that fits, centred,
 * standing on the box's bottom edge. It plays at speed_pct of the GIF's own
 * tempo (100 = its timing; not 0) while nobody types and steps on the key
 * presses the observer counts (status_observer.h) otherwise. A GIF that
 * cannot be opened (no pool memory for it) or whose first frame is malformed
 * logs why and shows nothing; malformed data later removes the sprite. Call
 * once. */
void beacon_sprite_create(lv_obj_t *parent, const lv_area_t *box, uint16_t speed_pct);
