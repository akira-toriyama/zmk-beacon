/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * CONFIG_BEACON_SPRITE_RECEIVE (ble_receiver.h): a sprite pack (sprite_pack.h)
 * received over BLE into the sprite room, the shield overlay's
 * sprite_partition, and played in place of the current sprite without a
 * reboot; at boot the room's pack comes back (sprite.c asks
 * beacon_receiver_stored_pack()). The device advertises connectably as
 * CONFIG_BT_DEVICE_NAME with the service's UUID, every ADV_INTERVAL. No
 * pairing and no encryption: anyone in range can connect and write the
 * room, and nothing else (the user's call, canon t-ky5n: a toy). A transfer
 * that fails leaves the embedded pack, or no sprite, until the next one.
 *
 * The service (UUIDs below; fields little-endian):
 * - control, write with response:
 *     0x01 BEGIN  u32 length  the pack's length. Freezes the sprite, erases
 *                             the room for it (a 4 KiB page takes about
 *                             0.22 s: 21 s for a full room, within ATT's 30 s
 *                             transaction timeout) and responds after the
 *                             erase. Rejected during a transfer, for 0 or
 *                             more than the room, and when the display work
 *                             queue did not freeze the sprite in time.
 *     0x02 COMMIT             writes the bytes received since the last
 *                             COMMIT, a page at most and a multiple of 4 (the
 *                             flash write block; else rejected), to flash.
 *                             The central sends no data until the response.
 *     0x03 END                commits the rest and, when the room holds the
 *                             pack BEGIN announced (length, header, CRC-32)
 *                             and the player opens it, shows it. The response
 *                             comes after that, and the report says how it
 *                             went.
 *   control, read: struct report, the device's state and the last transfer's
 *   result (a long read: it exceeds the default ATT MTU, which CoreBluetooth
 *   handles in one readValue).
 * - data, write without response: the pack's bytes in order, any chunking
 *   that COMMITs at multiples of 4 bytes, at most a page between COMMITs.
 * A sender: connect; read the report until `interval` is 12 (15 ms: the
 * device asks the central for the shield's preferred parameters
 * CONFIG_BT_CONN_PARAM_UPDATE_TIMEOUT after the connection, and a Mac applies
 * them within about a second); BEGIN; for every page of the pack, write it
 * and COMMIT; END; read the report; disconnect. A 206 KB pack took 20 s from
 * BEGIN to the picture (11 s of erase, 9 s of transfer; hardware
 * 2026-10-06).
 *
 * - The erase and the flash writes run in the control callbacks, on the BT
 *   RX work queue, while the central waits for a response and sends
 *   nothing: a page written from the data callback, with more data
 *   arriving, hung the device (hardware 2026-10-05, twice; not diagnosed).
 *   The nRF flash driver takes its slots between radio events
 *   (SOC_FLASH_NRF_RADIO_SYNC_TICKER), and a page erase halts the CPU:
 *   SOC_FLASH_NRF_PARTIAL_ERASE cuts it into 3 ms slices, without which a
 *   51-page erase (4.4 s) outlasted the 4 s supervision timeout (hardware
 *   2026-10-06).
 * - The observer's scan stops while a central is connected: continuous
 *   scanning reserves the whole radio timeline and starved a fresh 30 ms
 *   link once in 30 (hardware 2026-10-06). The status goes stale meanwhile;
 *   a transfer takes seconds, and the screen greys after a minute
 *   (prospector_screen.c).
 * - BEGIN freezes the sprite on the display work queue before the erase, so
 *   no decode reads erased flash, and the last picture stays on the screen
 *   through the transfer; END shows the new pack on that queue and waits for
 *   it, as does the fallback after a failure. Each wait is bounded
 *   (DISPLAY_WAIT_MS: a stalled display queue, say a screen dump the host
 *   stopped reading, must not hold the BT RX queue), and while an item is
 *   still outstanding the next one is refused: BEGIN never erases under a
 *   sprite that may still be decoding. A disconnect during a transfer falls
 *   back to the embedded pack, or no sprite.
 * - Advertising restarts from recycled(), not disconnected(): the connection
 *   object is still held there and bt_le_adv_start() fails with -ENOMEM.
 * - The GATT and connection callbacks run on the BT RX work queue one at a
 *   time, so the transfer state needs no lock; recycled() runs on the system
 *   work queue and touches none of it.
 */

#include <string.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#include <zmk/display.h>

#include "ble_receiver.h"
#include "sprite.h"
#include "sprite_pack.h"
#include "status_observer.h"

LOG_MODULE_REGISTER(beacon_receiver, LOG_LEVEL_INF);

BUILD_ASSERT(IS_ENABLED(CONFIG_SOC_FLASH_NRF_PARTIAL_ERASE),
             "the room's erase must run in slices, or a long erase drops the connection (ble_receiver.c)");
BUILD_ASSERT(IS_ENABLED(CONFIG_BT_GAP_AUTO_UPDATE_CONN_PARAMS) &&
                 IS_ENABLED(CONFIG_BT_GAP_PERIPHERAL_PREF_PARAMS),
             "the device asks the central for its preferred connection parameters itself (ble_receiver.c)");

#define RECEIVER_UUID(n)                                                                           \
    BT_UUID_128_ENCODE(0xb5e1c0de, 0x7a3f, 0x4c1b, 0x9d2e, 0x0f51a9c3e700ULL + (n))

static const struct bt_uuid_128 service_uuid = BT_UUID_INIT_128(RECEIVER_UUID(0));
static const struct bt_uuid_128 control_uuid = BT_UUID_INIT_128(RECEIVER_UUID(1));
static const struct bt_uuid_128 data_uuid = BT_UUID_INIT_128(RECEIVER_UUID(2));

/* 1022.5 ms, one of the intervals Apple's accessory guidelines list for a
 * central to find a device at. The user's pick (about a second, canon
 * t-ky5n): a daily swap needs no faster discovery. */
#define ADV_INTERVAL 0x0664
#define ADV_RETRY_MS 1000
#define REPORT_VERSION 1
#define ROOM_OFFSET FIXED_PARTITION_OFFSET(sprite_partition)
#define ROOM_SIZE FIXED_PARTITION_SIZE(sprite_partition)
#define PAGE_SIZE 4096
/* One flash slot a call. */
#define FLASH_WRITE_SIZE 256
#define DISPLAY_WAIT_MS 1000

BUILD_ASSERT(ROOM_OFFSET % PAGE_SIZE == 0 && ROOM_SIZE % PAGE_SIZE == 0,
             "the sprite room is erased by the page (ble_receiver.c)");

enum { OP_BEGIN = 0x01, OP_COMMIT = 0x02, OP_END = 0x03 };
enum { STATE_IDLE = 0, STATE_RECEIVING = 1 };
enum {
    RESULT_NONE = 0,
    RESULT_SHOWN = 1,
    /* Fewer bytes than BEGIN announced. */
    RESULT_SHORT = 2,
    /* The bytes are no pack of that length, or their CRC-32 fails. */
    RESULT_NOT_A_PACK = 3,
    /* The player did not open it: larger than the box, or no pool memory. */
    RESULT_REJECTED = 4,
    RESULT_FLASH_ERROR = 5,
    /* The central left during the transfer. */
    RESULT_DISCONNECTED = 6,
};
enum { SOURCE_NONE = 0, SOURCE_EMBEDDED = 1, SOURCE_ROOM = 2 };

struct report {
    uint8_t version;
    uint8_t state;
    uint8_t result;
    /* Where the sprite shown now came from. */
    uint8_t source;
    uint32_t expected;
    uint32_t received;
    /* The connection's interval (1.25 ms) and supervision timeout (10 ms). */
    uint16_t interval;
    uint16_t timeout;
    /* BEGIN's erase and the flash writes together, and the first flash
     * error (a Zephyr errno, negative). */
    uint32_t erase_ms;
    uint32_t store_ms;
    int32_t flash_err;
    /* The name of the sprite shown now, NUL-padded. */
    char name[SPRITE_PACK_NAME_LEN + 1];
} __packed;

static struct bt_conn *current;
static struct report xfer = {.version = REPORT_VERSION};

static const struct device *const flash = FIXED_PARTITION_DEVICE(sprite_partition);
/* Word-aligned, as the nRF flash driver wants its writes. */
static uint32_t page_buf[PAGE_SIZE / sizeof(uint32_t)];
static size_t page_fill;
static uint32_t stored;

/* The internal flash is mapped at its offsets. */
static const uint8_t *room(void) { return (const uint8_t *)(CONFIG_FLASH_BASE_ADDRESS + ROOM_OFFSET); }

const uint8_t *beacon_receiver_stored_pack(size_t *avail) {
    uint32_t total;

    *avail = ROOM_SIZE;
    return sprite_pack_valid(room(), ROOM_SIZE, &total) ? room() : NULL;
}

static void note_flash(int err) {
    if (err != 0 && xfer.flash_err == 0) {
        xfer.flash_err = err;
    }
}

static void flush_page(void) {
    const size_t len = ROUND_UP(page_fill, sizeof(uint32_t));
    const uint32_t start = k_uptime_get_32();

    memset((uint8_t *)page_buf + page_fill, 0xff, len - page_fill);
    for (size_t off = 0; off < len; off += FLASH_WRITE_SIZE) {
        note_flash(flash_write(flash, ROOM_OFFSET + stored + off, (const uint8_t *)page_buf + off,
                               MIN(FLASH_WRITE_SIZE, len - off)));
    }
    xfer.store_ms += k_uptime_get_32() - start;
    stored += page_fill;
    page_fill = 0;
}

/* The display work queue's part: freeze before the erase, show after END,
 * fall back after a failure; the control callbacks wait for each. One item
 * at a time: display_busy is set for the item until it has run, so a wait
 * that timed out leaves the next request refused rather than racing a late
 * run (the semaphore alone would take that run for the new item's). */
static K_SEM_DEFINE(display_done, 0, 1);
static atomic_t display_busy;
static bool shown;

static void done(void) {
    atomic_clear(&display_busy);
    k_sem_give(&display_done);
}

static void freeze(struct k_work *work) {
    ARG_UNUSED(work);
    beacon_sprite_freeze();
    done();
}

static void show(struct k_work *work) {
    ARG_UNUSED(work);
    shown = beacon_sprite_show(room(), ROOM_SIZE);
    done();
}

static void fall_back(struct k_work *work) {
    ARG_UNUSED(work);
    beacon_sprite_show(NULL, 0);
    done();
}

static K_WORK_DEFINE(freeze_work, freeze);
static K_WORK_DEFINE(show_work, show);
static K_WORK_DEFINE(fall_back_work, fall_back);

/* Runs work on the display work queue and waits for it: false when an
 * earlier item is still outstanding or this one does not finish in time. */
static bool on_display(struct k_work *work) {
    if (!atomic_cas(&display_busy, 0, 1)) {
        LOG_WRN("the display work queue still owes an answer");
        return false;
    }
    k_sem_reset(&display_done);
    k_work_submit_to_queue(zmk_display_work_q(), work);
    if (k_sem_take(&display_done, K_MSEC(DISPLAY_WAIT_MS)) == 0) {
        return true;
    }
    LOG_WRN("the display work queue did not answer in %d ms", DISPLAY_WAIT_MS);
    return false;
}

static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, RECEIVER_UUID(0)),
};

static const struct bt_data sd[] = {
    BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static const struct bt_le_adv_param adv_param =
    BT_LE_ADV_PARAM_INIT(BT_LE_ADV_OPT_CONN, ADV_INTERVAL, ADV_INTERVAL, NULL);

static void start_adv(struct k_work *work);

static K_WORK_DELAYABLE_DEFINE(start_adv_work, start_adv);

static void start_adv(struct k_work *work) {
    ARG_UNUSED(work);

    int err = bt_le_adv_start(&adv_param, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    if (err == 0 || err == -EALREADY) {
        LOG_INF("advertising, connectable, every %u ms", ADV_INTERVAL * 5 / 8);
        return;
    }
    LOG_ERR("advertising start failed (%d), retrying in %d ms", err, ADV_RETRY_MS);
    k_work_schedule(&start_adv_work, K_MSEC(ADV_RETRY_MS));
}

static void fill_report(struct bt_conn *conn) {
    struct bt_conn_info info;
    const uint8_t *pack = beacon_sprite_pack();
    size_t len = 0;
    const char *name = "";

    xfer.source = pack == NULL ? SOURCE_NONE : pack == room() ? SOURCE_ROOM : SOURCE_EMBEDDED;
    /* During a transfer the room's pack is being erased under the frozen
     * sprite: its name is gone, the picture is still on the screen. After a
     * failed one the fallback has run (end(), disconnected() wait for it)
     * unless the display queue stalled; then the name is as unreliable. */
    if (pack != NULL && (pack != room() || (xfer.state != STATE_RECEIVING &&
                                              !atomic_get(&display_busy)))) {
        name = sprite_pack_name(pack, &len);
    }
    memset(xfer.name, 0, sizeof(xfer.name));
    memcpy(xfer.name, name, len);
    if (bt_conn_get_info(conn, &info) == 0) {
        xfer.interval = info.le.interval;
        xfer.timeout = info.le.timeout;
    }
}

static ssize_t read_control(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
                            uint16_t len, uint16_t offset) {
    /* A long read comes as several requests; the first fills the live
     * fields (a write between the parts would still show in the later
     * ones; a sender reads between its own writes). */
    if (offset == 0) {
        fill_report(conn);
    }
    return bt_gatt_attr_read(conn, attr, buf, len, offset, &xfer, sizeof(xfer));
}

static void end_transfer(uint8_t result) {
    xfer.state = STATE_IDLE;
    xfer.result = result;
}

static ssize_t begin(const uint8_t *in, uint16_t len) {
    if (len != 5) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }
    const uint32_t expected = sys_get_le32(&in[1]);
    if (expected == 0 || expected > ROOM_SIZE || xfer.state == STATE_RECEIVING) {
        return BT_GATT_ERR(BT_ATT_ERR_WRITE_REQ_REJECTED);
    }
    if (!on_display(&freeze_work)) {
        /* The sprite may still read the room: no erase under it. */
        return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
    }
    xfer.state = STATE_RECEIVING;
    xfer.result = RESULT_NONE;
    xfer.expected = expected;
    xfer.received = 0;
    xfer.erase_ms = 0;
    xfer.store_ms = 0;
    xfer.flash_err = 0;
    page_fill = 0;
    stored = 0;
    const uint32_t start = k_uptime_get_32();
    note_flash(flash_erase(flash, ROOM_OFFSET, ROUND_UP(expected, PAGE_SIZE)));
    xfer.erase_ms = k_uptime_get_32() - start;
    /* ROUND_UP computes in unsigned long: the cast keeps %u honest. */
    LOG_INF("begin: %u bytes expected, %u pages erased in %u ms (%d)", expected,
            (unsigned int)(ROUND_UP(expected, PAGE_SIZE) / PAGE_SIZE), xfer.erase_ms,
            xfer.flash_err);
    return len;
}

static ssize_t end(uint16_t len) {
    uint32_t total;

    if (xfer.state != STATE_RECEIVING) {
        return BT_GATT_ERR(BT_ATT_ERR_WRITE_REQ_REJECTED);
    }
    flush_page();
    if (xfer.flash_err != 0) {
        end_transfer(RESULT_FLASH_ERROR);
    } else if (xfer.received != xfer.expected) {
        end_transfer(RESULT_SHORT);
    } else if (!sprite_pack_valid(room(), xfer.expected, &total) || total != xfer.expected) {
        end_transfer(RESULT_NOT_A_PACK);
    } else {
        shown = false;
        on_display(&show_work);
        end_transfer(shown ? RESULT_SHOWN : RESULT_REJECTED);
    }
    /* The player falls back by itself when it refuses a pack. */
    if (xfer.result != RESULT_SHOWN && xfer.result != RESULT_REJECTED) {
        on_display(&fall_back_work);
    }
    LOG_INF("end: %u of %u bytes, flash %u ms (%d), result %u", xfer.received, xfer.expected,
            xfer.store_ms, xfer.flash_err, xfer.result);
    return len;
}

static ssize_t write_control(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                             const void *buf, uint16_t len, uint16_t offset, uint8_t flags) {
    ARG_UNUSED(conn);
    ARG_UNUSED(attr);
    ARG_UNUSED(flags);
    const uint8_t *in = buf;

    if (offset != 0) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }
    if (len < 1) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }
    switch (in[0]) {
    case OP_BEGIN:
        return begin(in, len);
    case OP_COMMIT:
        /* The flash writes in blocks of 4: a page that ends between them
         * would put every later write off its alignment. */
        if (xfer.state != STATE_RECEIVING || page_fill % sizeof(uint32_t) != 0) {
            return BT_GATT_ERR(BT_ATT_ERR_WRITE_REQ_REJECTED);
        }
        flush_page();
        return len;
    case OP_END:
        return end(len);
    default:
        return BT_GATT_ERR(BT_ATT_ERR_NOT_SUPPORTED);
    }
}

static ssize_t write_data(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf,
                          uint16_t len, uint16_t offset, uint8_t flags) {
    ARG_UNUSED(conn);
    ARG_UNUSED(attr);
    ARG_UNUSED(offset);
    ARG_UNUSED(flags);

    if (xfer.state != STATE_RECEIVING || xfer.received + len > xfer.expected ||
        page_fill + len > PAGE_SIZE) {
        return BT_GATT_ERR(BT_ATT_ERR_WRITE_REQ_REJECTED);
    }
    memcpy((uint8_t *)page_buf + page_fill, buf, len);
    page_fill += len;
    xfer.received += len;
    return len;
}

BT_GATT_SERVICE_DEFINE(
    receiver_service, BT_GATT_PRIMARY_SERVICE(&service_uuid),
    BT_GATT_CHARACTERISTIC(&control_uuid.uuid, BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
                           BT_GATT_PERM_READ | BT_GATT_PERM_WRITE, read_control, write_control,
                           NULL),
    BT_GATT_CHARACTERISTIC(&data_uuid.uuid, BT_GATT_CHRC_WRITE_WITHOUT_RESP, BT_GATT_PERM_WRITE,
                           NULL, write_data, NULL));

static void connected(struct bt_conn *conn, uint8_t err) {
    if (err != 0) {
        LOG_ERR("connection failed: 0x%02x", err);
        return;
    }
    current = bt_conn_ref(conn);
    /* Scheduled before any GATT traffic (header); the system work queue
     * stops the scan. */
    beacon_observer_scan_pause(true);
    LOG_INF("connected");
}

static void disconnected(struct bt_conn *conn, uint8_t reason) {
    if (conn != current) {
        return;
    }
    LOG_INF("disconnected: reason 0x%02x, %u of %u bytes", reason, xfer.received, xfer.expected);
    bt_conn_unref(current);
    current = NULL;
    if (xfer.state == STATE_RECEIVING) {
        end_transfer(RESULT_DISCONNECTED);
        on_display(&fall_back_work);
    }
    beacon_observer_scan_pause(false);
}

static void recycled(void) { k_work_schedule(&start_adv_work, K_NO_WAIT); }

static void le_param_updated(struct bt_conn *conn, uint16_t interval, uint16_t latency,
                             uint16_t timeout) {
    ARG_UNUSED(conn);
    LOG_INF("parameters: interval %u latency %u timeout %u", interval, latency, timeout);
}

BT_CONN_CB_DEFINE(receiver_conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
    .recycled = recycled,
    .le_param_updated = le_param_updated,
};

/* After status_observer.c's bt_enable() (APPLICATION 50). */
static int ble_receiver_init(void) {
    k_work_schedule(&start_adv_work, K_NO_WAIT);
    return 0;
}

SYS_INIT(ble_receiver_init, APPLICATION, 51);
