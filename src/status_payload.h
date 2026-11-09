/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * The status payload: 26 bytes of BLE manufacturer data, written by
 * status_broadcaster.c on the device that runs the keyboard's keymap and read
 * by status_observer.c on the Prospector Dongle. The byte offsets are
 * prospector-zmk-module v2.2.3's layout (include/zmk/status_advertisement.h
 * there, CENTRAL_SIDE="AUX"). Only the fields below are written and read;
 * every other byte stays 0. A new layout is a new version byte (canon task
 * t-xe2q), both ends in one commit; a field that takes over a byte of the
 * layout keeps the version (keystrokes, modifiers).
 */

#pragma once

#define BEACON_PAYLOAD_LEN 26

/* The broadcaster advertises the payload this often and refreshes it right
 * after a key press; a refresh goes on air at the next advertising event, so a
 * press adds no payload. The observer's KEYS_DELTA_MAX and the sprite's
 * KEY_GRACE_MS are chosen against it. */
#define BEACON_PAYLOAD_INTERVAL_MS 200

/* [0..3]: company id 0xFFFF, then the magic AB CD. */
#define BEACON_PAYLOAD_PREFIX_INIT 0xff, 0xff, 0xab, 0xcd
#define BEACON_PAYLOAD_PREFIX_LEN 4

/* [4]: major.minor of the layout, 2.2. */
#define BEACON_PAYLOAD_OFFSET_VERSION 4
#define BEACON_PAYLOAD_VERSION 0x22

/* Battery of each half in percent, 0 = no reading: the broadcaster's own
 * level in the byte BEACON_OWN_BATTERY names (a keyboard that is not a split
 * and a central half, the left by default), and ZMK's split slots in the
 * others, slot 0 first (status_broadcaster.c). A dongle sends none of its own:
 * left = slot 0, right = slot 1. A slot is the order the central first heard
 * each half in, before pairing, kept in its settings (ZMK's
 * zmk_ble_put_peripheral_addr(); canon CLAUDE.md, "slot index"; on canon's
 * dongle left/right measured 2026-09-27 by powering each half off). A central
 * half has one peripheral, in slot 0 whichever half it is. The field's
 * meaning grew from the peripherals to the broadcaster's own level without a
 * new version byte (canon task t-r9dt): 0 stays no reading. */
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
 * carries both. The field took over the layout's wpm_value byte without a new
 * version byte (canon task t-7c05). */
#define BEACON_PAYLOAD_OFFSET_KEYSTROKES 24

/* Modifiers held, as the HID modifier byte (bit 0 LCTRL .. bit 7 RGUI; the
 * layout's modifier_flags byte): only those confirmed as held, a modifier
 * tapped on a mod-tap thumb never shows (status_broadcaster.c, "Modifiers").
 * The sprite shows its back animation while this is not 0 (sprite.c). */
#define BEACON_PAYLOAD_OFFSET_MODIFIERS 23
