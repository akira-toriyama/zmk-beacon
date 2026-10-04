/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * The screen dump (CONFIG_BEACON_SCREEN_DUMP, screen_dump.c), entered from the
 * CDC ACM rate callback of bootloader_on_1200_baud.c: the class driver keeps
 * one rate callback per device, so that one hands this rate on.
 */

#pragma once

#include <zephyr/device.h>

#define BEACON_SCREEN_DUMP_BAUD 2400

/* Called on the USB device work queue inside the SET_LINE_CODING data stage,
 * so it only hands the dump to ZMK's display work queue; a request while a
 * dump runs is dropped. dev is the port that asked, and gets the dump. */
void beacon_screen_dump_request(const struct device *dev);
