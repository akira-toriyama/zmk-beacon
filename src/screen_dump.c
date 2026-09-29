/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * Screen dump: when the host sets a CDC ACM port to 2400 baud, the whole
 * screen is rendered once more and each band LVGL flushes to the panel is sent
 * over that port, straight from LVGL's draw buffer. canon's
 * scripts/dongle.py shot reads it and writes a PNG.
 *
 * Wire format, the contract with dongle.py (change both together and bump
 * DUMP_VERSION); integers are little-endian:
 *
 *   start  A5 'S' 'C' 'R'  version:u8  format:u8  width:u16  height:u16
 *   band   A5 'B' 'N' 'D'  x1:u16 y1:u16 x2:u16 y2:u16, then the band's pixels
 *          row by row, 2 bytes each: (x2-x1+1) * (y2-y1+1) * 2 bytes
 *   end    A5 'E' 'N' 'D'  bands:u16  crc32:u32 (IEEE, as zlib's crc32(), over
 *          the pixel bytes of every band in order)
 *   error  A5 'E' 'R' 'R'  reason:u8 (enum dump_error), in place of the rest
 *
 * format 1 = RGB565 high byte first (the panel's order: LV_COLOR_16_SWAP),
 * 2 = low byte first. The bands of one dump tile the screen exactly once. The
 * port can carry older output ahead of the start record (log lines, the tail
 * of an abandoned dump): the reader skips to the start or error tag.
 *
 * - No pixel buffer of its own: a copy of the screen (280x240 RGB565,
 *   134,400 B) exceeds the RAM any build has left, and the panel cannot be
 *   read back (the MIPI DBI bus is write-only). An invalidation of the whole
 *   screen joins every pending area into one (lv_refr_join_area()), so the
 *   next refresh renders the whole screen, in bands of the draw buffer's
 *   height.
 * - The tap is LV_EVENT_FLUSH_FINISH (LVGL 9.3.0-dev f1db87ee, lv_refr.c
 *   call_flush_cb()): LVGL has swapped the band's RGB565 bytes in place and
 *   run the flush callback, synchronous without CONFIG_LV_Z_FLUSH_THREAD
 *   (Zephyr's lvgl_flush_display() returns after display_write()), and has not
 *   yet switched buf_act: the active draw buffer holds the band, reshaped to
 *   its width and height (partial render mode). lv_display_flush_is_last()
 *   marks the refresh's last band; lv_display_flush_ready() does not clear it.
 * - All of it runs on ZMK's display work queue, LVGL's only thread
 *   (LV_USE_OS=0): the screen stands still while the bands are sent.
 * - Bytes go into the TX ring only between USB transfers: uart_irq_tx_ready()
 *   and uart_fifo_fill() under one irq_lock() against the USB work queue.
 *   Zephyr's legacy cdc_acm.c frees a transfer's ring space as it starts the
 *   transfer (tx_work_handler), and the nRF USBD reads that memory one 64-byte
 *   packet at a time as the host takes them (nrf_usbd_common.c), so a fill
 *   during a transfer would overwrite bytes not yet sent. uart_poll_out() drops
 *   bytes on a full ring instead. The TX interrupt flag is on for the dump
 *   only: with no UART callback set it gates uart_irq_tx_ready() and nothing
 *   else, and nothing else on this port uses the interrupt API (the log backend
 *   and the console write with poll_out). A host that takes no transfer for
 *   STALL_MS ends the dump without its end record.
 * - A logging image writes its log to the same port. Every active log backend
 *   is paused for the dump, once the log thread has finished the line it may be
 *   in the middle of (log_buffered_cnt() counts a message until it is fully
 *   processed); the lines logged meanwhile are dropped. The log thread activates
 *   the backends only when it starts (CONFIG_LOG_PROCESS_THREAD_STARTUP_DELAY_MS
 *   after boot) and then writes the boot log out, so a dump waits for that.
 */

#include <limits.h>
#include <string.h>

#include <lvgl.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_backend.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/util.h>
#include <zmk/display.h>

#include "screen_dump.h"

LOG_MODULE_REGISTER(beacon_screen_dump, LOG_LEVEL_INF);

BUILD_ASSERT(!IS_ENABLED(CONFIG_LV_Z_FLUSH_THREAD),
             "the screen dump reads a band after its flush returns: no flush thread");

#define DUMP_VERSION 1
#define FORMAT_RGB565_HIGH_FIRST 1
#define FORMAT_RGB565_LOW_FIRST 2
#define TAG_LEN 4
/* A reading host takes every transfer within milliseconds; this long without
 * one means it stopped reading. */
#define STALL_MS 1000
/* A dump asked for before the log thread started waits for it, polling, up to
 * this uptime. */
#define LOG_START_LIMIT_MS 5000
#define LOG_START_POLL_MS 50
/* The refresh after the invalidation starts within LV_DEF_REFR_PERIOD (33 ms);
 * none by then means the display is not refreshing. */
#define REFRESH_TIMEOUT_MS 2000
/* log_thread_trigger() wakes the log thread, which then drops the pending
 * lines at once (every backend is paused); the bound keeps a flood of new
 * lines from holding the dump back. */
#define LOG_SETTLE_MS 200

enum dump_error {
    DUMP_ERROR_NO_DISPLAY = 1,
    DUMP_ERROR_COLOR_FORMAT = 2,
    DUMP_ERROR_NO_REFRESH = 3,
    DUMP_ERROR_BAND_BUFFER = 4,
};

static const uint8_t tag_start[TAG_LEN] = {0xA5, 'S', 'C', 'R'};
static const uint8_t tag_band[TAG_LEN] = {0xA5, 'B', 'N', 'D'};
static const uint8_t tag_end[TAG_LEN] = {0xA5, 'E', 'N', 'D'};
static const uint8_t tag_error[TAG_LEN] = {0xA5, 'E', 'R', 'R'};

/* Set by the request on the USB work queue, cleared when the dump ends. */
static atomic_t busy;
static const struct device *port;

/* Display work queue only. */
static bool armed;
static bool stalled;
static bool listening;
static uint16_t bands;
static uint32_t crc;
static uint32_t sent;
static int64_t start_ms;
#if IS_ENABLED(CONFIG_LOG)
static uint32_t paused_backends;
#endif

/* Queues data for the host, between transfers only (the header says why).
 * False once the host has stopped reading: nothing more goes out in this
 * dump. */
static bool send(const uint8_t *data, size_t len) {
    int64_t progress_ms = k_uptime_get();

    while (len > 0 && !stalled) {
        const unsigned int key = irq_lock();
        const int n = uart_irq_tx_ready(port) > 0
                          ? uart_fifo_fill(port, data, (int)MIN(len, (size_t)INT_MAX))
                          : 0;

        irq_unlock(key);
        if (n > 0) {
            data += n;
            len -= n;
            sent += n;
            progress_ms = k_uptime_get();
        } else if (n < 0 || k_uptime_get() - progress_ms >= STALL_MS) {
            stalled = true;
        } else {
            k_msleep(1);
        }
    }
    return !stalled;
}

static bool send_record(const uint8_t tag[TAG_LEN], const uint8_t *fields, size_t len) {
    return send(tag, TAG_LEN) && send(fields, len);
}

static void pause_logs(void) {
#if IS_ENABLED(CONFIG_LOG)
    const int count = MIN(log_backend_count_get(), 32);

    paused_backends = 0;
    for (int i = 0; i < count; i++) {
        const struct log_backend *backend = log_backend_get(i);

        if (log_backend_is_active(backend)) {
            log_backend_deactivate(backend);
            paused_backends |= BIT(i);
        }
    }
#if IS_ENABLED(CONFIG_LOG_MODE_DEFERRED)
    log_thread_trigger();
    for (int ms = 0; ms < LOG_SETTLE_MS && log_buffered_cnt() > 0; ms++) {
        k_msleep(1);
    }
#endif
#endif
}

static void resume_logs(void) {
#if IS_ENABLED(CONFIG_LOG)
    const int count = MIN(log_backend_count_get(), 32);

    for (int i = 0; i < count; i++) {
        if (paused_backends & BIT(i)) {
            const struct log_backend *backend = log_backend_get(i);

            log_backend_activate(backend, backend->cb->ctx);
        }
    }
    paused_backends = 0;
#endif
}

/* Whether the log thread has started: it activates the autostart backends
 * first thing. */
static bool logs_started(void) {
#if IS_ENABLED(CONFIG_LOG)
    for (int i = 0; i < log_backend_count_get(); i++) {
        const struct log_backend *backend = log_backend_get(i);

        if (backend->autostart && !log_backend_is_active(backend)) {
            return false;
        }
    }
#endif
    return true;
}

static void refresh_timed_out(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(timeout_work, refresh_timed_out);

/* error 0: the dump ended, sent whole or cut short by a host that stopped
 * reading. */
static void end_dump(uint8_t error) {
    armed = false;
    k_work_cancel_delayable(&timeout_work);
    if (error != 0) {
        send_record(tag_error, &error, 1);
    }
    uart_irq_tx_disable(port);
    resume_logs();

    if (error != 0) {
        LOG_WRN("screen dump failed: reason %u, %u bytes sent", error, sent);
    } else if (stalled) {
        LOG_WRN("screen dump cut short: the host stopped reading after %u bytes", sent);
    } else {
        LOG_INF("screen dump: %u bands, %u bytes in %u ms", bands, sent,
                (uint32_t)(k_uptime_get() - start_ms));
    }
    atomic_clear(&busy);
}

static void refresh_timed_out(struct k_work *work) {
    ARG_UNUSED(work);

    if (armed) {
        end_dump(DUMP_ERROR_NO_REFRESH);
    }
}

static void band_flushed(lv_event_t *e) {
    if (!armed) {
        return;
    }

    lv_display_t *disp = lv_event_get_current_target(e);
    const lv_area_t *area = lv_event_get_param(e);
    const lv_draw_buf_t *buf = lv_display_get_buf_active(disp);
    const int32_t w = lv_area_get_width(area);
    const int32_t h = lv_area_get_height(area);
    const uint32_t row_bytes = (uint32_t)w * 2;

    if (buf == NULL || (int32_t)buf->header.w != w || (int32_t)buf->header.h != h ||
        buf->header.stride < row_bytes) {
        end_dump(DUMP_ERROR_BAND_BUFFER);
        return;
    }

    uint8_t fields[8];

    sys_put_le16(area->x1, &fields[0]);
    sys_put_le16(area->y1, &fields[2]);
    sys_put_le16(area->x2, &fields[4]);
    sys_put_le16(area->y2, &fields[6]);
    bool ok = send_record(tag_band, fields, sizeof(fields));

    for (int32_t y = 0; ok && y < h; y++) {
        const uint8_t *row = buf->data + (size_t)y * buf->header.stride;

        crc = crc32_ieee_update(crc, row, row_bytes);
        ok = send(row, row_bytes);
    }
    bands++;

    if (lv_display_flush_is_last(disp)) {
        uint8_t end[6];

        sys_put_le16(bands, &end[0]);
        sys_put_le32(crc, &end[2]);
        send_record(tag_end, end, sizeof(end));
        end_dump(0);
    }
}

static void start_dump(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(start_work, start_dump);

static void start_dump(struct k_work *work) {
    ARG_UNUSED(work);
    lv_display_t *disp = lv_display_get_default();

    if (!logs_started() && k_uptime_get() < LOG_START_LIMIT_MS) {
        k_work_schedule_for_queue(zmk_display_work_q(), &start_work, K_MSEC(LOG_START_POLL_MS));
        return;
    }

    start_ms = k_uptime_get();
    sent = 0;
    bands = 0;
    crc = 0;
    stalled = false;
    pause_logs();
    uart_irq_tx_enable(port);

    if (disp == NULL) {
        end_dump(DUMP_ERROR_NO_DISPLAY);
        return;
    }
    if (lv_display_get_color_format(disp) != LV_COLOR_FORMAT_RGB565) {
        end_dump(DUMP_ERROR_COLOR_FORMAT);
        return;
    }

    uint8_t fields[6];

    fields[0] = DUMP_VERSION;
    fields[1] = IS_ENABLED(CONFIG_LV_COLOR_16_SWAP) ? FORMAT_RGB565_HIGH_FIRST
                                                     : FORMAT_RGB565_LOW_FIRST;
    sys_put_le16(lv_display_get_horizontal_resolution(disp), &fields[2]);
    sys_put_le16(lv_display_get_vertical_resolution(disp), &fields[4]);
    if (!send_record(tag_start, fields, sizeof(fields))) {
        end_dump(0);
        return;
    }

    if (!listening) {
        lv_display_add_event_cb(disp, band_flushed, LV_EVENT_FLUSH_FINISH, NULL);
        listening = true;
    }
    armed = true;
    lv_obj_invalidate(lv_display_get_screen_active(disp));
    k_work_schedule_for_queue(zmk_display_work_q(), &timeout_work, K_MSEC(REFRESH_TIMEOUT_MS));
}

void beacon_screen_dump_request(const struct device *dev) {
    if (!atomic_cas(&busy, 0, 1)) {
        return;
    }
    port = dev;
    if (k_work_schedule_for_queue(zmk_display_work_q(), &start_work, K_NO_WAIT) < 0) {
        atomic_clear(&busy);
    }
}
