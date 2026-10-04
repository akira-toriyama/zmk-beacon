/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * The PWM backlight (backlight.c), lit at boot at
 * CONFIG_BEACON_BACKLIGHT_BRIGHTNESS.
 */

#pragma once

#include <stdint.h>

/* The backlight's PWM duty in percent; 0 stops the PWM and holds the pin low
 * (zephyr drivers/pwm/pwm_nrfx.c), which a transmissive panel shows as dark. */
int beacon_backlight_set(uint8_t percent);
