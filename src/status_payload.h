/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * The status payload: 26 bytes of BLE manufacturer data, written by
 * status_broadcaster.c on the keyboard's split central and read by
 * status_observer.c on the Prospector Dongle. The layout is the one
 * prospector-zmk-module v2.2.3 sent (include/zmk/status_advertisement.h there,
 * CENTRAL_SIDE="AUX"), kept byte for byte so that the observer accepts a
 * keyboard still running that module. Only the fields below are written and
 * read; every other byte stays 0. A new layout is a new version byte (canon
 * task t-xe2q).
 */

#pragma once

#define BEACON_PAYLOAD_LEN 26

/* [0..3]: company id 0xFFFF, then the magic AB CD. */
#define BEACON_PAYLOAD_PREFIX_INIT 0xff, 0xff, 0xab, 0xcd
#define BEACON_PAYLOAD_PREFIX_LEN 4

/* [4]: major.minor of the layout, 2.2. */
#define BEACON_PAYLOAD_OFFSET_VERSION 4
#define BEACON_PAYLOAD_VERSION 0x22

/* Battery of each half in percent, 0 = no reading. Left = ZMK split slot 0,
 * right = slot 1: the slot is the half's first-pairing order (canon CLAUDE.md,
 * "slot index"; left/right measured 2026-09-27 by powering each half off). */
#define BEACON_PAYLOAD_OFFSET_LEFT 5
#define BEACON_PAYLOAD_OFFSET_RIGHT 12

/* Highest active layer: its index, and the first 4 bytes of its name, 0-padded
 * (the reason canon's keymap keeps display-name within 4 ASCII characters). */
#define BEACON_PAYLOAD_OFFSET_LAYER 6
#define BEACON_PAYLOAD_OFFSET_LAYER_NAME 15
#define BEACON_PAYLOAD_LAYER_NAME_LEN 4

/* Typing speed in words per minute, ZMK's value as is (app/src/wpm.c: keycode
 * releases only, recomputed every second over a window reset every 5 s, so it
 * reads 0 1-6 s after the last one). wpm.c keeps it in a uint8_t, so a rate
 * above 255 wraps there, not here. */
#define BEACON_PAYLOAD_OFFSET_WPM 24
