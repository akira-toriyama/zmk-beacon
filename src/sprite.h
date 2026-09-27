/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * The status screen's GIF sprite (CONFIG_BEACON_SPRITE). Display work queue
 * only: LVGL is not thread-safe here (LV_USE_OS=0).
 */

#pragma once

#include <stdint.h>

#include <lvgl.h>

/* Creates the sprite inside parent, in box (parent coordinates), scaled by the
 * largest integer factor that fits and top-centred; it starts paused on its
 * first frame. NULL: the GIF could not be opened or does not fit at 1x, and
 * the screen goes on without a sprite. Call once. */
lv_obj_t *beacon_sprite_create(lv_obj_t *parent, const lv_area_t *box);

/* Tempo in percent of the GIF's own: 0 freezes on the current frame, 100 is
 * the GIF's timing, 200 twice as fast. No-op without a sprite. */
void beacon_sprite_set_speed(uint16_t percent);
