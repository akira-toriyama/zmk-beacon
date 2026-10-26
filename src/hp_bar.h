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
#include <stddef.h>
#include <stdint.h>

#include <lvgl.h>
#include <zephyr/sys/util.h>

#include "font_pkmn.h"
#include "status_observer.h"

/* The name row exists with an embedded sprite that has a name, or with the
 * receiver, whose packs name themselves (Kconfig BEACON_HP_BAR_NAME_ROW); the
 * text comes from the pack being played (sprite.c, beacon_hp_bar_set_name). */
#define BEACON_HP_BAR_NAME_ROW IS_ENABLED(CONFIG_BEACON_HP_BAR_NAME_ROW)

/* The box's distance from the panel's side and bottom edges: the panel's
 * corners are rounded, and a box 2 px from the edge lost its own corners
 * (hardware 2026-09-28). */
#define BEACON_HP_BAR_MARGIN_PX 12
/* One row ("HP" and the bar), or a second one for the name (hp_bar.c places
 * the rows): unscii 16 for an ASCII name, the katakana font for the rest,
 * whose line is the taller (the dakuten rows above the cell) and sets the
 * height: the border, the name row's top 24 px inside it, the line, 2 px
 * under it, the border. */
#define BEACON_HP_BAR_BOX_H (BEACON_HP_BAR_NAME_ROW ? 2 + 24 + BEACON_FONT_PKMN_LINE_H + 2 + 2 : 28)
/* Bytes of the name row's text, the pack's name field (sprite_pack.h): 15
 * unscii 16 cells or 5 katakana, all inside the box. */
#define BEACON_HP_BAR_NAME_BYTES 15
/* The first panel row the box covers. */
#define BEACON_HP_BAR_TOP(screen_h) ((screen_h) - BEACON_HP_BAR_MARGIN_PX - BEACON_HP_BAR_BOX_H)

/* Creates the box in parent, showing no reading. Call once. */
void beacon_hp_bar_create(lv_obj_t *parent, int32_t screen_w);

/* Shows the observer's state; fresh is whether a payload arrived recently
 * enough to trust it. Returns whether anything shown changed. */
bool beacon_hp_bar_show(const struct beacon_status *now, bool fresh);

/* The name row's text: the first BEACON_HP_BAR_NAME_BYTES of name (len bytes
 * of UTF-8, not NUL-terminated; a pack's name is never longer), copied, in the
 * font its first byte of 0x80 or above picks; 0 bytes clears it. Nothing
 * without the row. */
void beacon_hp_bar_set_name(const char *name, size_t len);
