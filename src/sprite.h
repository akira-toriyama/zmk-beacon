/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * The status screen's sprite (CONFIG_BEACON_SPRITE, sprite.c). Display
 * work queue only: LVGL is not thread-safe here (LV_USE_OS=0).
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <lvgl.h>

/* Creates the sprite as the first child of screen (an lv_obj_create(NULL)
 * screen: blit()'s conditions, sprite.c), in box (screen coordinates), which
 * must hold the canvas at 1x: scaled to the largest size of its proportions
 * that fits, centred, standing on the box's bottom edge. It plays the pack in
 * the sprite room when that is whole (CONFIG_BEACON_SPRITE_RECEIVE), else the
 * embedded one (CONFIG_BEACON_SPRITE_EMBEDDED), else nothing until
 * beacon_sprite_show() brings one. It plays at SPEED_PCT of the GIF's own
 * tempo (sprite.c) while nobody types and steps on the key presses the
 * observer counts (status_observer.h) otherwise, and names the HP bar's row
 * (hp_bar.h) after the pack. A pack that cannot be opened (no pool memory for
 * it, larger than the box) logs why and shows nothing; malformed data later
 * removes the sprite. Call once, after the HP bar exists. */
void beacon_sprite_create(lv_obj_t *screen, const lv_area_t *box);

/* Plays the pack at data, memory that stays readable and unchanged while
 * shown (avail bytes of it readable), front animation first, in place of
 * the current sprite; NULL falls back to the embedded pack, or to no sprite.
 * Unfreezes. False, with the fallback shown, when data does not open or
 * exceeds the box. */
bool beacon_sprite_show(const uint8_t *data, size_t avail);

/* Stops decoding until the next beacon_sprite_show(): the canvas stays on
 * the screen and the pack's memory may go (the receiver erases the room). */
void beacon_sprite_freeze(void);

/* The pack being played, NULL for none (any thread: one pointer read). */
const uint8_t *beacon_sprite_pack(void);
