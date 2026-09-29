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

/* The broadcaster sends the payload this often, and right after a key press;
 * the observer's KEYS_DELTA_MAX and the sprite's KEY_GRACE_MS are chosen
 * against it. */
#define BEACON_PAYLOAD_INTERVAL_MS 200

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

/* Keystrokes: key presses on either half, any key, counted modulo 256. The
 * observer takes the difference between consecutive payloads, so a lost
 * payload loses no press and a payload that replaced one not yet on air
 * carries both. The module's wpm_value byte: a broadcaster before this field
 * sends 0 there (no steps, batteries as before, which is why the version byte
 * stays 0x22), except the images of 2026-09-27/28 (adec978 to 3500c22) and a
 * keyboard still on the module, which send a WPM whose changes would read as
 * presses; neither has been in use since (canon task t-7c05). */
#define BEACON_PAYLOAD_OFFSET_KEYSTROKES 24
