/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * The sprite receiver (CONFIG_BEACON_SPRITE_RECEIVE, ble_receiver.c): the
 * sprite room, a flash partition, and the BLE service that fills it.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

/* The pack in the sprite room when it is whole (sprite_pack_valid()), else
 * NULL; *avail the room's readable length. Memory-mapped flash: readable
 * from any thread, unchanged until the next transfer erases the room. */
const uint8_t *beacon_receiver_stored_pack(size_t *avail);
