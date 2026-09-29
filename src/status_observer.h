/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * Keyboard state received by status_observer.c. Safe to read from any thread;
 * the screen reads it on ZMK's display work queue.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

struct beacon_status {
    /* False until the first valid status payload arrives. */
    bool received;
    /* Battery of each half in percent; 0 = no reading (the half has not
     * reported since it connected, or it is disconnected). */
    uint8_t left;
    uint8_t right;
    /* k_uptime_get() of the last valid payload. */
    int64_t last_ms;
    /* Key presses on the keyboard since the first payload, accumulated from
     * the payload's 8-bit counter (status_payload.h) as the difference between
     * consecutive payloads, so a lost payload drops no press. A keyboard that
     * rebooted restarts its counter: a difference above KEYS_DELTA_MAX
     * (status_observer.c) is taken for that and not counted, a smaller one
     * reads as that many presses once. */
    uint32_t keystrokes;
    /* k_uptime_get() of the last payload that raised keystrokes. */
    int64_t key_ms;
};

void beacon_status_get(struct beacon_status *out);
