/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * BLE observer for the keyboard's status advertisement, the payload of
 * prospector-zmk-module v2.2.3 that canon's Imprint Dongle broadcasts. This
 * file owns the Bluetooth bring-up: CONFIG_ZMK_BLE=n compiles out ZMK's
 * bt_enable() callers (ble.c, and split/bluetooth/peripheral.c through
 * ZMK_SPLIT_BLE) and with them the connectable advertisement, so the device
 * never advertises. The active scan transmits SCAN_REQs only, from a fresh
 * non-resolvable private address.
 *
 * Payload contract (manufacturer data, 26 bytes), read from the module at
 * v2.2.3 (include/zmk/status_advertisement.h:22-39,
 * src/status_advertisement.c:767-777) on 2026-09-26:
 *   [0..3]  FF FF AB CD (company id 0xFFFF, then the magic in byte order)
 *   [4]     version 0x22
 *   [5]     left half battery, [12] right half battery. The broadcaster maps
 *           them with CENTRAL_SIDE="AUX": ZMK split slot 0 goes to [5], slot 1
 *           to [12]. 0 = no reading.
 * No charging state is carried. Other fields are not read.
 *
 * - ACTIVE scan: while ZMK advertises, the broadcaster puts the payload in the
 *   scan response only (canon's FORCE_NAME_IN_AD workaround selects that
 *   layout); a passive scan receives nothing. When ZMK does not advertise, the
 *   module advertises on its own with the payload in the AD, which this scan
 *   receives as well, except in its idle "host connected" mode, where the AD
 *   carries the name only. That mode needs a connected BLE host profile, and
 *   canon's dongle has none (ZMK_BLE_PROFILE_COUNT = BT_MAX_PAIRED 2 - 2 split
 *   peripherals = 0, so ZMK always advertises; zmk app/include/zmk/ble.h).
 * - No duplicate filter: every BT_LE_SCAN_* helper sets FILTER_DUPLICATE, and
 *   the controller then reports each address and PDU type once, so later
 *   payload changes would never arrive. The parameters are spelled out here.
 * - The scan callback runs on the cooperative BT RX work queue (1200-byte
 *   stack without BT_SETTINGS): parse, copy under the spinlock, return. No
 *   logging and no LVGL there.
 * - Version filter: byte 4 is the module's major.minor only (0x22 for every
 *   2.2.x; the patch number sits in byte 7). A major or minor bump shows
 *   "no data" rather than wrong numbers; a patch bump passes unchecked, so the
 *   offsets above are re-read whenever canon bumps the module.
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

LOG_MODULE_REGISTER(beacon_observer, LOG_LEVEL_INF);

BUILD_ASSERT(IS_ENABLED(CONFIG_BT_OBSERVER), "the status observer needs CONFIG_BT_OBSERVER=y");

#define PAYLOAD_LEN 26
#define PAYLOAD_VERSION 0x22
#define OFFSET_VERSION 4
#define OFFSET_LEFT 5
#define OFFSET_RIGHT 12

#define SCAN_RETRY_MS 1000
#define STATS_PERIOD_MS 60000

static const uint8_t payload_prefix[] = {0xff, 0xff, 0xab, 0xcd};

static const struct bt_le_scan_param scan_param = {
    .type = BT_LE_SCAN_TYPE_ACTIVE,
    .options = BT_LE_SCAN_OPT_NONE,
    .interval = BT_GAP_SCAN_FAST_WINDOW,
    .window = BT_GAP_SCAN_FAST_WINDOW,
};

static struct k_spinlock status_lock;
static struct beacon_status status;

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
    LOG_INF("%ld status payloads in %d s", (long)atomic_set(&payload_count, 0),
            STATS_PERIOD_MS / 1000);
    k_work_schedule(&stats_work, K_MSEC(STATS_PERIOD_MS));
}

static bool find_payload(struct bt_data *data, void *user_data) {
    const uint8_t **payload = user_data;

    if (data->type != BT_DATA_MANUFACTURER_DATA || data->data_len < PAYLOAD_LEN ||
        memcmp(data->data, payload_prefix, sizeof(payload_prefix)) != 0 ||
        data->data[OFFSET_VERSION] != PAYLOAD_VERSION) {
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
    const uint8_t left = payload[OFFSET_LEFT];
    const uint8_t right = payload[OFFSET_RIGHT];

    k_spinlock_key_t key = k_spin_lock(&status_lock);
    const bool changed = !status.received || status.left != left || status.right != right;
    status.received = true;
    status.left = left;
    status.right = right;
    status.last_ms = k_uptime_get();
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
