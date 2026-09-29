/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * BLE observer for the keyboard's status advertisement (status_payload.h),
 * which status_broadcaster.c sends from canon's Imprint Dongle. This file owns
 * the Bluetooth bring-up: CONFIG_ZMK_BLE=n compiles out ZMK's bt_enable()
 * callers (ble.c, and split/bluetooth/peripheral.c through ZMK_SPLIT_BLE) and
 * with them the connectable advertisement, so the device never advertises,
 * and the scan is passive, so it transmits nothing at all.
 *
 * Only the prefix, the version byte, both halves' battery bytes and the
 * keystroke counter are read. 0 = no reading. No charging state is carried.
 *
 * - Passive scan: the broadcaster sends the payload in the AD of a
 *   non-connectable, non-scannable ADV_NONCONN_IND (status_broadcaster.c),
 *   so there is no scan response to ask for.
 * - No duplicate filter: every BT_LE_SCAN_* helper sets FILTER_DUPLICATE, and
 *   the controller then reports each address and PDU type once, so later
 *   payload changes would never arrive. The parameters are spelled out here.
 * - The scan callback runs on the cooperative BT RX work queue (1200-byte
 *   stack without BT_SETTINGS): parse, copy under the spinlock, return. No
 *   logging and no LVGL there.
 * - Version filter: the version byte is the layout's major.minor. A layout
 *   change (t-xe2q) bumps it, and an old observer then shows "no data" rather
 *   than wrong numbers.
 * - Any keyboard that sends this payload is accepted (keyboard_id is not
 *   filtered): one keyboard in range is assumed.
 */

#include <string.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net_buf.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#include "status_observer.h"
#include "status_payload.h"

LOG_MODULE_REGISTER(beacon_observer, LOG_LEVEL_INF);

BUILD_ASSERT(IS_ENABLED(CONFIG_BT_OBSERVER), "the status observer needs CONFIG_BT_OBSERVER=y");

#define SCAN_RETRY_MS 1000
#define STATS_PERIOD_MS 60000
/* A larger difference in one payload is the keyboard's counter starting over
 * (a reboot: the Imprint Dongle reflashed under a running observer read as
 * 182 presses, hardware 2026-09-29), not typing: payloads come every
 * BEACON_PAYLOAD_INTERVAL_MS and the occasional lost one leaves no room for
 * that many presses. Such a payload only sets the new reference. */
#define KEYS_DELTA_MAX 32

static const uint8_t payload_prefix[] = {BEACON_PAYLOAD_PREFIX_INIT};

static const struct bt_le_scan_param scan_param = {
    .type = BT_LE_SCAN_TYPE_PASSIVE,
    .options = BT_LE_SCAN_OPT_NONE,
    .interval = BT_GAP_SCAN_FAST_WINDOW,
    .window = BT_GAP_SCAN_FAST_WINDOW,
};

static struct k_spinlock status_lock;
static struct beacon_status status;
/* Under status_lock: the counter byte of the last payload, and the counter
 * restarts dropped so far (a difference above KEYS_DELTA_MAX) with the last
 * one's difference, for the minute line. */
static uint8_t last_keys;
static uint32_t key_restarts;
static uint8_t key_restart_jump;

/* Valid payloads since the last stats line (logging builds only). */
static atomic_t payload_count;

void beacon_status_get(struct beacon_status *out) {
    k_spinlock_key_t key = k_spin_lock(&status_lock);
    *out = status;
    k_spin_unlock(&status_lock, key);
}

static void log_change(struct k_work *work) {
    ARG_UNUSED(work);
    struct beacon_status now;
    beacon_status_get(&now);
    LOG_INF("battery left %u right %u", now.left, now.right);
}

static K_WORK_DEFINE(log_change_work, log_change);

static void log_stats(struct k_work *work);

static K_WORK_DELAYABLE_DEFINE(stats_work, log_stats);

static void log_stats(struct k_work *work) {
    ARG_UNUSED(work);
    k_spinlock_key_t key = k_spin_lock(&status_lock);
    const uint32_t keystrokes = status.keystrokes;
    const uint32_t restarts = key_restarts;
    const uint8_t jump = key_restart_jump;
    k_spin_unlock(&status_lock, key);

    LOG_INF("%ld status payloads in %d s; so far %u keystrokes, %u counter restarts (last jump %u)",
            (long)atomic_set(&payload_count, 0), STATS_PERIOD_MS / 1000, keystrokes, restarts,
            jump);
    k_work_schedule(&stats_work, K_MSEC(STATS_PERIOD_MS));
}

static bool find_payload(struct bt_data *data, void *user_data) {
    const uint8_t **payload = user_data;

    if (data->type != BT_DATA_MANUFACTURER_DATA || data->data_len < BEACON_PAYLOAD_LEN ||
        memcmp(data->data, payload_prefix, sizeof(payload_prefix)) != 0 ||
        data->data[BEACON_PAYLOAD_OFFSET_VERSION] != BEACON_PAYLOAD_VERSION) {
        return true;
    }

    *payload = data->data;
    return false;
}

static void scan_cb(const bt_addr_le_t *addr, int8_t rssi, uint8_t adv_type,
                    struct net_buf_simple *buf) {
    ARG_UNUSED(addr);
    ARG_UNUSED(rssi);
    ARG_UNUSED(adv_type);

    const uint8_t *payload = NULL;
    bt_data_parse(buf, find_payload, &payload);
    if (payload == NULL) {
        return;
    }

    /* payload points into buf, valid only during this callback. */
    const uint8_t left = payload[BEACON_PAYLOAD_OFFSET_LEFT];
    const uint8_t right = payload[BEACON_PAYLOAD_OFFSET_RIGHT];
    const uint8_t keys = payload[BEACON_PAYLOAD_OFFSET_KEYSTROKES];
    const int64_t now_ms = k_uptime_get();

    k_spinlock_key_t key = k_spin_lock(&status_lock);
    const bool changed = !status.received || status.left != left || status.right != right;
    /* The first payload only sets the reference: what came before it is
     * unknown. Modulo 256, as the keyboard counts. */
    const uint8_t delta = status.received ? (uint8_t)(keys - last_keys) : 0;
    if (delta > KEYS_DELTA_MAX) {
        key_restarts++;
        key_restart_jump = delta;
    } else if (delta != 0) {
        status.keystrokes += delta;
        status.key_ms = now_ms;
    }
    last_keys = keys;
    status.received = true;
    status.left = left;
    status.right = right;
    status.last_ms = now_ms;
    k_spin_unlock(&status_lock, key);

    if (IS_ENABLED(CONFIG_LOG)) {
        atomic_inc(&payload_count);
        if (changed) {
            k_work_submit(&log_change_work);
        }
    }
}

static void start_scan(struct k_work *work);

static K_WORK_DELAYABLE_DEFINE(start_scan_work, start_scan);

static void start_scan(struct k_work *work) {
    ARG_UNUSED(work);

    int err = bt_le_scan_start(&scan_param, scan_cb);
    if (err == 0 || err == -EALREADY) {
        LOG_INF("scanning");
        return;
    }

    LOG_ERR("scan start failed (%d), retrying in %d ms", err, SCAN_RETRY_MS);
    k_work_schedule(&start_scan_work, K_MSEC(SCAN_RETRY_MS));
}

static int status_observer_init(void) {
    /* Synchronous: without BT_SETTINGS the stack is ready on return.
     * -EALREADY: a consumer config brought another caller back; scan anyway. */
    int err = bt_enable(NULL);
    if (err < 0 && err != -EALREADY) {
        LOG_ERR("bt_enable failed: %d", err);
        return err;
    }

    k_work_schedule(&start_scan_work, K_NO_WAIT);
    if (IS_ENABLED(CONFIG_LOG)) {
        k_work_schedule(&stats_work, K_MSEC(STATS_PERIOD_MS));
    }
    return 0;
}

SYS_INIT(status_observer_init, APPLICATION, 50);
