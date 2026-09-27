/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * Status broadcaster for the keyboard's split central (canon's Imprint
 * Dongle): the payload of status_payload.h on a second advertising set next
 * to ZMK's own connectable one. Legacy PDU, non-connectable, non-scannable
 * (ADV_NONCONN_IND), so the Prospector Dongle's observer receives it with a
 * plain scan; refreshed every CONFIG_BEACON_STATUS_BROADCAST_INTERVAL_MS.
 * Measured on hardware 2026-09-27 as the t-eray spike (projects t-eray): the
 * halves reconnected as before, no advertising error in 5 reboots and a half
 * power cycle, the Prospector Dongle received 255-269 payloads a minute.
 *
 * - CONFIG_BT_EXT_ADV: the host then shares one pool of
 *   CONFIG_BT_EXT_ADV_MAX_ADV_SET sets between ZMK's legacy bt_le_adv_start()
 *   and bt_le_ext_adv_create(); the consumer sets the pool to 2. The legacy
 *   API keeps working (the host translates it to the extended HCI commands).
 * - Creation time: the set is created from a settings commit handler at
 *   commit priority 1, which runs on the main thread inside ZMK's
 *   settings_load() (app/src/main.c) after every priority-0 commit: 'bt' has set BT_DEV_READY and the identity
 *   (before it, create returns -EAGAIN) and ZMK's 'ble' has made its first
 *   bt_le_adv_start(). The host's adv_new() is not thread safe (zephyr
 *   subsys/bluetooth/host/adv.c), so the create must not overlap that first
 *   start. The handler only kicks the work queue below. It is registered after
 *   bt_enable() (ZMK: APPLICATION 50), whose settings_init() drops earlier
 *   dynamic handlers. Without CONFIG_SETTINGS nothing commits, hence the
 *   Kconfig dependency.
 * - Own work queue: bt_le_ext_adv_set_data() is a synchronous HCI command,
 *   and the host flags blocking calls from the system work queue for removal
 *   (hci_core.c, the k_sys_work_q check in bt_hci_cmd_send_sync()).
 * - Address: a per-set non-resolvable private address, generated at start
 *   and never rotated (no BT_PRIVACY). The observer does not look at it.
 * - On any failure the set is deleted and recreated 500 ms later, so a retry
 *   never updates a set that never advertised. Once up, only set_data runs.
 *   An update that arrives before the previous one went on air replaces it
 *   (CONFIG_BT_CTLR_ADV_DATA_BUF_MAX=1); that is expected.
 * - The payload goes out from the first start, with 0 for a half that has not
 *   reported yet (the observer shows "--" for 0). ZMK raises the battery event
 *   once per half after every (re)connect and with 0 on disconnect.
 * - The WPM byte is read from ZMK's wpm.c on every refresh (Kconfig selects
 *   ZMK_WPM). wpm.c counts keycode releases (zmk_keycode_state_changed) only:
 *   &vkey, layer and mouse keys do not count.
 */

#include <string.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/keymap.h>
#include <zmk/wpm.h>

#include "status_payload.h"

LOG_MODULE_REGISTER(beacon_broadcaster, LOG_LEVEL_INF);

BUILD_ASSERT(CONFIG_BT_EXT_ADV_MAX_ADV_SET >= 2,
             "CONFIG_BT_EXT_ADV_MAX_ADV_SET must be 2: ZMK's own advertising takes one set");

#define RETRY_MS 500
#define STATS_PERIOD_MS 60000
#define STACK_SIZE 1536
#define SLOT_COUNT 2

/* Flags 0x06 (general discoverable, no BR/EDR) as the module sent; with the
 * 26-byte manufacturer data the AD is exactly the 31-byte legacy maximum. */
static const uint8_t ad_flags[] = {BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR};
static uint8_t payload[BEACON_PAYLOAD_LEN] = {BEACON_PAYLOAD_PREFIX_INIT, BEACON_PAYLOAD_VERSION};
static const struct bt_data ad[] = {
    BT_DATA(BT_DATA_FLAGS, ad_flags, sizeof(ad_flags)),
    BT_DATA(BT_DATA_MANUFACTURER_DATA, payload, sizeof(payload)),
};

static const struct bt_le_adv_param adv_param = BT_LE_ADV_PARAM_INIT(
    BT_LE_ADV_OPT_NONE, BT_GAP_MS_TO_ADV_INTERVAL(CONFIG_BEACON_STATUS_BROADCAST_INTERVAL_MS),
    BT_GAP_MS_TO_ADV_INTERVAL(CONFIG_BEACON_STATUS_BROADCAST_INTERVAL_MS), NULL);

K_THREAD_STACK_DEFINE(bcast_stack, STACK_SIZE);
static struct k_work_q bcast_q;

static struct bt_le_ext_adv *adv;

/* Written by the battery listener on ZMK's event thread, read on bcast_q. */
static atomic_t battery[SLOT_COUNT];

/* Counters for the stats line (logging builds only). */
static atomic_t updates_ok;
static atomic_t updates_err;
static atomic_t start_failures;

static void fill_payload(void) {
    payload[BEACON_PAYLOAD_OFFSET_LEFT] = (uint8_t)atomic_get(&battery[0]);
    payload[BEACON_PAYLOAD_OFFSET_RIGHT] = (uint8_t)atomic_get(&battery[1]);
    payload[BEACON_PAYLOAD_OFFSET_WPM] = (uint8_t)zmk_wpm_get_state();

    zmk_keymap_layer_index_t index = zmk_keymap_highest_layer_active();
    const char *name = zmk_keymap_layer_name(zmk_keymap_layer_index_to_id(index));
    char *out = (char *)&payload[BEACON_PAYLOAD_OFFSET_LAYER_NAME];

    payload[BEACON_PAYLOAD_OFFSET_LAYER] = index;
    memset(out, 0, BEACON_PAYLOAD_LAYER_NAME_LEN);
    if (name != NULL) {
        strncpy(out, name, BEACON_PAYLOAD_LAYER_NAME_LEN);
    }
}

static int start_set(void) {
    if (!bt_is_ready()) {
        return -EAGAIN;
    }

    int err = bt_le_ext_adv_create(&adv_param, NULL, &adv);
    if (err) {
        LOG_ERR("create failed (%d)", err);
        adv = NULL;
        atomic_inc(&start_failures);
        return err;
    }

    fill_payload();
    err = bt_le_ext_adv_set_data(adv, ad, ARRAY_SIZE(ad), NULL, 0);
    if (!err) {
        err = bt_le_ext_adv_start(adv, BT_LE_EXT_ADV_START_DEFAULT);
    }
    if (err) {
        LOG_ERR("first set_data/start failed (%d)", err);
        int del = bt_le_ext_adv_delete(adv);
        if (del) {
            LOG_ERR("delete failed (%d)", del);
        }
        adv = NULL;
        atomic_inc(&start_failures);
        return err;
    }

    LOG_INF("advertising every %d ms", CONFIG_BEACON_STATUS_BROADCAST_INTERVAL_MS);
    return 0;
}

static void tick(struct k_work *work);

static K_WORK_DELAYABLE_DEFINE(tick_work, tick);

static void tick(struct k_work *work) {
    ARG_UNUSED(work);

    if (adv == NULL) {
        if (start_set() != 0) {
            k_work_reschedule_for_queue(&bcast_q, &tick_work, K_MSEC(RETRY_MS));
            return;
        }
    } else {
        fill_payload();
        int err = bt_le_ext_adv_set_data(adv, ad, ARRAY_SIZE(ad), NULL, 0);
        atomic_inc(err ? &updates_err : &updates_ok);
    }

    k_work_reschedule_for_queue(&bcast_q, &tick_work,
                                K_MSEC(CONFIG_BEACON_STATUS_BROADCAST_INTERVAL_MS));
}

static void log_stats(struct k_work *work);

static K_WORK_DELAYABLE_DEFINE(stats_work, log_stats);

static void log_stats(struct k_work *work) {
    ARG_UNUSED(work);
    LOG_INF("%s, %ld updates ok, %ld failed, %ld start failures, battery %u/%u, in %d s",
            adv != NULL ? "advertising" : "not advertising", (long)atomic_set(&updates_ok, 0),
            (long)atomic_set(&updates_err, 0), (long)atomic_get(&start_failures),
            (unsigned int)atomic_get(&battery[0]), (unsigned int)atomic_get(&battery[1]),
            STATS_PERIOD_MS / 1000);
    k_work_schedule(&stats_work, K_MSEC(STATS_PERIOD_MS));
}

static int on_commit(void) {
    k_work_reschedule_for_queue(&bcast_q, &tick_work, K_NO_WAIT);
    return 0;
}

static int on_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg) {
    ARG_UNUSED(name);
    ARG_UNUSED(len);
    ARG_UNUSED(read_cb);
    ARG_UNUSED(cb_arg);
    return 0;
}

static struct settings_handler commit_handler = {
    .name = "beacon",
    .h_set = on_set,
    .h_commit = on_commit,
};

static int status_broadcaster_init(void) {
    k_work_queue_init(&bcast_q);
    k_work_queue_start(&bcast_q, bcast_stack, K_THREAD_STACK_SIZEOF(bcast_stack),
                       K_LOWEST_APPLICATION_THREAD_PRIO, NULL);
    k_thread_name_set(&bcast_q.thread, "beacon_bcast");
    if (IS_ENABLED(CONFIG_LOG)) {
        k_work_schedule(&stats_work, K_MSEC(STATS_PERIOD_MS));
    }
    return settings_register_with_cprio(&commit_handler, 1);
}

/* After ZMK's bt_enable() at APPLICATION 50. */
SYS_INIT(status_broadcaster_init, APPLICATION, 90);

static int battery_listener(const zmk_event_t *eh) {
    const struct zmk_peripheral_battery_state_changed *ev =
        as_zmk_peripheral_battery_state_changed(eh);

    if (ev != NULL && ev->source < SLOT_COUNT) {
        atomic_set(&battery[ev->source], ev->state_of_charge);
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(beacon_broadcaster, battery_listener);
ZMK_SUBSCRIPTION(beacon_broadcaster, zmk_peripheral_battery_state_changed);
