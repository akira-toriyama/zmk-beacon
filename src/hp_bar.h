/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * The HP bar reading (CONFIG_BEACON_READINGS_HP_BAR): "HP" and a bar in a dark
 * box after the games' battle screen, showing one level. A sibling of the
 * sprite, not a part of it: what the bar shows (prospector_screen.c maps the
 * batteries to it) and what plays above it change independently. Display work
 * queue only: LVGL is not thread-safe here (LV_USE_OS=0).
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <lvgl.h>

#define BEACON_HP_BAR_H 28

/* Creates the box, width wide and BEACON_HP_BAR_H tall, bottom-centred in
 * parent bottom_margin above its bottom edge, showing no reading. Call once. */
lv_obj_t *beacon_hp_bar_create(lv_obj_t *parent, int32_t width, int32_t bottom_margin);

/* level: 1-100, or 0 for no reading (an empty track). fresh false greys the
 * "HP". Returns whether anything shown changed. */
bool beacon_hp_bar_show(uint8_t level, bool fresh);
