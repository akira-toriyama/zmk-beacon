/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * Status broadcaster for the keyboard's split central (canon's Imprint
 * Dongle): the payload of status_payload.h on a second advertising set next
 * to ZMK's own connectable one. Legacy PDU, non-connectable, non-scannable
 * (ADV_NONCONN_IND) with the payload in its AD, so the Prospector Dongle's
 * observer receives it with a passive scan; refreshed every
 * BEACON_PAYLOAD_INTERVAL_MS.
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
 * - Keystrokes: every key press on either half counts once, at the press
 *   (zmk_position_state_changed, which the split central raises for the
 *   halves' keys), and kicks the tick, so the new count is on air at the next
 *   advertising event, at most one interval (plus the controller's random
 *   delay of up to 10 ms) after the press instead of up to two. This
 *   listener is linked before ZMK's own (a module's sources precede
 *   app/ in zmk.map, checked 2026-09-29), so it sees a press before a hold-tap
 *   or a combo captures or consumes it; combo.c release_pressed_keys()
 *   re-raises the second and later captured presses from the first listener
 *   (ZMK_EVENT_RAISE), which the (position, timestamp) memory below skips
 *   (the timestamp is stamped once in split/central.c and copied on every
 *   re-raise). A press during the tick's own set_data queues one more run (a
 *   running work item may be resubmitted, zephyr kernel/work.c
 *   submit_to_queue_locked()). Until the set advertises the kick is skipped:
 *   creating the set must not overlap ZMK's first bt_le_adv_start() (above),
 *   and a failed start retries at RETRY_MS, not at typing rate.
 * - Modifiers: the explicit modifiers of zmk_keycode_state_changed (a
 *   modifier key, a mod-tap's hold, a sticky key; not the LS(A) kind) go
 *   into the payload once confirmed as held: pressed for MOD_CONFIRM_MS, or
 *   another, non-modifier keycode pressed meanwhile. The second is the
 *   hold-tap's interrupt: it decides before it lets the captured key through,
 *   so the keycode order tells; the first is its timer (the keymap's
 *   tapping-term-ms, hold-preferred mod-taps on the thumbs with
 *   hold-while-undecided, which press the modifier at key down and release
 *   it again when the key turns out a tap, canon config/imprint.keymap). A
 *   tap on such a thumb thus never shows, and a lone modifier shows
 *   MOD_CONFIRM_MS late. A confirmation and a release both kick the tick;
 *   a confirmed modifier stays in the payload for MOD_LATCH_MS from its
 *   confirmation even when released sooner, because a set_data before the
 *   next advertising event replaces the one before it (above): a short
 *   Shift+letter would otherwise never go on air. Keycode events and the
 *   confirm timer run on the system work queue; what the tick reads is
 *   atomic.
 */

#include <stdint.h>
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
#include <zmk/events/keycode_state_changed.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/keymap.h>

#include "status_payload.h"

LOG_MODULE_REGISTER(beacon_broadcaster, LOG_LEVEL_INF);

BUILD_ASSERT(CONFIG_BT_EXT_ADV_MAX_ADV_SET >= 2,
             "CONFIG_BT_EXT_ADV_MAX_ADV_SET must be 2: ZMK's own advertising takes one set");

#define RETRY_MS 500
#define LOG_PERIOD_MS 60000
#define STACK_SIZE 1536
#define SLOT_COUNT 2
/* The keymap's tapping term: a modifier held this long is a hold. */
#define MOD_CONFIRM_MS 200
/* One advertising interval, the controller's random delay (up to 10 ms) and
 * a margin, so that a confirmed modifier goes on air at least once. */
#define MOD_LATCH_MS 250
#define MOD_COUNT 8

/* Flags 0x06 (general discoverable, no BR/EDR) as the module sent; with the
 * 26-byte manufacturer data the AD is exactly the 31-byte legacy maximum. */
static const uint8_t ad_flags[] = {BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR};
static uint8_t payload[BEACON_PAYLOAD_LEN] = {BEACON_PAYLOAD_PREFIX_INIT, BEACON_PAYLOAD_VERSION};
static const struct bt_data ad[] = {
    BT_DATA(BT_DATA_FLAGS, ad_flags, sizeof(ad_flags)),
    BT_DATA(BT_DATA_MANUFACTURER_DATA, payload, sizeof(payload)),
};

static const struct bt_le_adv_param adv_param =
    BT_LE_ADV_PARAM_INIT(BT_LE_ADV_OPT_NONE, BT_GAP_MS_TO_ADV_INTERVAL(BEACON_PAYLOAD_INTERVAL_MS),
                         BT_GAP_MS_TO_ADV_INTERVAL(BEACON_PAYLOAD_INTERVAL_MS), NULL);

K_THREAD_STACK_DEFINE(bcast_stack, STACK_SIZE);
static struct k_work_q bcast_q;

static struct bt_le_ext_adv *adv;

/* Written by the listener on ZMK's event thread, read on bcast_q. */
static atomic_t battery[SLOT_COUNT];
/* Key presses since boot; the payload carries the low byte. */
static atomic_t keystrokes;
/* Set once the set advertises: from then on a press may kick the tick. */
static atomic_t advertising;

/* Modifiers (header). System work queue: the bits pressed and not yet
 * confirmed, when each was pressed, and the bits confirmed and still held.
 * Read by the tick: the confirmed bits, the bits latched by the last
 * confirmation and until when (k_uptime_get_32() ms). */
static uint8_t mods_pending;
static int64_t mods_pressed_at[MOD_COUNT];
static uint8_t mods_held;
static atomic_t mods_confirmed;
static atomic_t mods_latched;
static atomic_t mods_latch_until;
static atomic_t mods_confirmations;

/* The last presses counted, so that a re-raised one is not counted again
 * (header). Combos capture a few presses at most, even with an undecided
 * hold-tap holding some back first (canon: 2-key combos); every position
 * event runs on the system work queue, so no lock. The timestamp is a
 * millisecond: two presses of one key in the same millisecond (a link stall
 * delivering press, release, press together) count once. */
#define SEEN_PRESSES 8
static struct {
    uint32_t position;
    int64_t timestamp;
} seen[SEEN_PRESSES];
static unsigned int seen_next;

static bool seen_before(const struct zmk_position_state_changed *pos) {
    for (unsigned int i = 0; i < SEEN_PRESSES; i++) {
        if (seen[i].timestamp == pos->timestamp && seen[i].position == pos->position) {
            return true;
        }
    }
    seen[seen_next].position = pos->position;
    seen[seen_next].timestamp = pos->timestamp;
    seen_next = (seen_next + 1) % SEEN_PRESSES;
    return false;
}

/* Counters for the stats line (logging builds only). */
static atomic_t updates_ok;
static atomic_t updates_err;
static atomic_t start_failures;

static uint8_t modifiers_on_air(void) {
    uint8_t mods = (uint8_t)atomic_get(&mods_confirmed);

    if ((int32_t)(atomic_get(&mods_latch_until) - k_uptime_get_32()) > 0) {
        mods |= (uint8_t)atomic_get(&mods_latched);
    }
    return mods;
}

static void fill_payload(void) {
    payload[BEACON_PAYLOAD_OFFSET_LEFT] = (uint8_t)atomic_get(&battery[0]);
    payload[BEACON_PAYLOAD_OFFSET_RIGHT] = (uint8_t)atomic_get(&battery[1]);
    payload[BEACON_PAYLOAD_OFFSET_KEYSTROKES] = (uint8_t)atomic_get(&keystrokes);
    payload[BEACON_PAYLOAD_OFFSET_MODIFIERS] = modifiers_on_air();

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

    atomic_set(&advertising, 1);
    LOG_INF("advertising every %d ms", BEACON_PAYLOAD_INTERVAL_MS);
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

    k_work_reschedule_for_queue(&bcast_q, &tick_work, K_MSEC(BEACON_PAYLOAD_INTERVAL_MS));
}

static void log_stats(struct k_work *work);

static K_WORK_DELAYABLE_DEFINE(stats_work, log_stats);

static void log_stats(struct k_work *work) {
    ARG_UNUSED(work);
    LOG_INF("%s, %ld updates ok, %ld failed, %ld start failures, battery %u/%u, %lu keystrokes, "
            "%lu modifiers confirmed, in %d s",
            adv != NULL ? "advertising" : "not advertising", (long)atomic_set(&updates_ok, 0),
            (long)atomic_set(&updates_err, 0), (long)atomic_get(&start_failures),
            (unsigned int)atomic_get(&battery[0]), (unsigned int)atomic_get(&battery[1]),
            (unsigned long)atomic_get(&keystrokes), (unsigned long)atomic_get(&mods_confirmations),
            LOG_PERIOD_MS / 1000);
    k_work_schedule(&stats_work, K_MSEC(LOG_PERIOD_MS));
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
        k_work_schedule(&stats_work, K_MSEC(LOG_PERIOD_MS));
    }
    return settings_register_with_cprio(&commit_handler, 1);
}

/* After ZMK's bt_enable() at APPLICATION 50. */
SYS_INIT(status_broadcaster_init, APPLICATION, 90);

static void kick(k_timeout_t delay) {
    if (atomic_get(&advertising)) {
        k_work_reschedule_for_queue(&bcast_q, &tick_work, delay);
    }
}

static void confirm_mods(uint8_t mods) {
    mods_pending &= ~mods;
    mods_held |= mods;
    atomic_or(&mods_confirmed, mods);
    atomic_set(&mods_latched, mods_held);
    atomic_set(&mods_latch_until, k_uptime_get_32() + MOD_LATCH_MS);
    atomic_inc(&mods_confirmations);
    kick(K_NO_WAIT);
}

static void confirm_due(struct k_work *work);

static K_WORK_DELAYABLE_DEFINE(confirm_work, confirm_due);

/* Confirms the pending modifiers whose term is over and waits for the next. */
static void confirm_due(struct k_work *work) {
    ARG_UNUSED(work);
    const int64_t now = k_uptime_get();
    int64_t next = INT64_MAX;
    uint8_t due = 0;

    for (unsigned int i = 0; i < MOD_COUNT; i++) {
        if (mods_pending & BIT(i)) {
            const int64_t at = mods_pressed_at[i] + MOD_CONFIRM_MS;

            if (at <= now) {
                due |= BIT(i);
            } else {
                next = MIN(next, at);
            }
        }
    }
    if (due) {
        confirm_mods(due);
    }
    if (next != INT64_MAX) {
        k_work_schedule(&confirm_work, K_MSEC(next - now));
    }
}

static void on_keycode(const struct zmk_keycode_state_changed *ev) {
    const uint8_t mods = ev->explicit_modifiers;

    if (mods == 0) {
        if (ev->state && mods_pending) {
            confirm_mods(mods_pending);
        }
        return;
    }
    if (ev->state) {
        for (unsigned int i = 0; i < MOD_COUNT; i++) {
            if (mods & BIT(i)) {
                mods_pressed_at[i] = ev->timestamp;
            }
        }
        mods_pending |= mods;
        /* A pending work item is due no later than this one's term. */
        if (!k_work_delayable_is_pending(&confirm_work)) {
            k_work_schedule(&confirm_work, K_MSEC(MOD_CONFIRM_MS));
        }
        return;
    }
    mods_pending &= ~mods;
    if (mods_held & mods) {
        mods_held &= ~mods;
        atomic_and(&mods_confirmed, (atomic_val_t) ~(unsigned int)mods);
        /* The release goes out when the latch is over, now if it is. */
        const int32_t latched_ms = (int32_t)(atomic_get(&mods_latch_until) - k_uptime_get_32());

        kick(K_MSEC(MAX(latched_ms, 0)));
    }
}

static int event_listener(const zmk_event_t *eh) {
    const struct zmk_keycode_state_changed *kc = as_zmk_keycode_state_changed(eh);
    if (kc != NULL) {
        on_keycode(kc);
        return ZMK_EV_EVENT_BUBBLE;
    }

    const struct zmk_peripheral_battery_state_changed *bat =
        as_zmk_peripheral_battery_state_changed(eh);
    if (bat != NULL) {
        if (bat->source < SLOT_COUNT) {
            atomic_set(&battery[bat->source], bat->state_of_charge);
        }
        return ZMK_EV_EVENT_BUBBLE;
    }

    const struct zmk_position_state_changed *pos = as_zmk_position_state_changed(eh);
    if (pos != NULL && pos->state && !seen_before(pos)) {
        atomic_inc(&keystrokes);
        kick(K_NO_WAIT);
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(beacon_broadcaster, event_listener);
ZMK_SUBSCRIPTION(beacon_broadcaster, zmk_peripheral_battery_state_changed);
ZMK_SUBSCRIPTION(beacon_broadcaster, zmk_position_state_changed);
ZMK_SUBSCRIPTION(beacon_broadcaster, zmk_keycode_state_changed);
