/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * The status screen's battery readings: one HP bar in a box along the bottom
 * (hp_bar.c). The box's height is a compile-time constant so that the sprite
 * box above it is known at build time. Display work queue only: LVGL is not
 * thread-safe here (LV_USE_OS=0).
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <lvgl.h>

#include "status_observer.h"

/* CONFIG_BEACON_SPRITE_NAME depends on BEACON_SPRITE: a build without a sprite
 * has no definition at all. */
#ifdef CONFIG_BEACON_SPRITE_NAME
#define BEACON_HP_BAR_NAME CONFIG_BEACON_SPRITE_NAME
#else
#define BEACON_HP_BAR_NAME ""
#endif

/* The box's distance from the panel's side and bottom edges: the panel's
 * corners are rounded, and a box 2 px from the edge lost its own corners
 * (hardware 2026-09-28). */
#define BEACON_HP_BAR_MARGIN_PX 12
/* One row ("HP" and the bar), or a second one for the name (sizeof counts the
 * terminator: 1 is the empty string). hp_bar.c places the rows. */
#define BEACON_HP_BAR_BOX_H (sizeof(BEACON_HP_BAR_NAME) > 1 ? 46 : 28)
/* The first panel row the box covers. */
#define BEACON_HP_BAR_TOP(screen_h) ((screen_h) - BEACON_HP_BAR_MARGIN_PX - BEACON_HP_BAR_BOX_H)

/* Creates the box in parent, showing no reading. Call once. */
void beacon_hp_bar_create(lv_obj_t *parent, int32_t screen_w);

/* Shows the observer's state; fresh is whether a payload arrived recently
 * enough to trust it. Returns whether anything shown changed. */
bool beacon_hp_bar_show(const struct beacon_status *now, bool fresh);
