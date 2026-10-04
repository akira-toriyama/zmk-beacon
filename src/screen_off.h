/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * Screen off (screen_off.c): the screen goes dark when the keyboard has not
 * been typed on for CONFIG_BEACON_SCREEN_OFF_AFTER_S and lights with the next
 * key press. Display work queue only.
 */

#pragma once

#include <stdbool.h>

/* Starts the timer that turns the screen off and on. Call once, from
 * zmk_display_status_screen(). */
void beacon_screen_off_start(void);

/* Whether the screen is dark: false while it is lit or still fading. */
bool beacon_screen_is_off(void);
