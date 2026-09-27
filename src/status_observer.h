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
    /* Typing speed in words per minute, 0 within about 6 s of the last key
     * (status_payload.h). */
    uint8_t wpm;
    /* k_uptime_get() of the last valid payload. */
    int64_t last_ms;
};

void beacon_status_get(struct beacon_status *out);
