/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * The status screen's GIF sprite: the file named by CONFIG_BEACON_SPRITE_GIF,
 * embedded at build time (CMakeLists.txt), decoded with LVGL's gifdec into
 * its ARGB8888 canvas, and drawn by blit() straight into the display buffer,
 * scaled by nearest neighbour to fill its box. An own player and an own draw
 * rather than lv_gif and lv_image: the widget invalidates on every frame even
 * when nothing changed (half the frames of the test GIF), has no tempo
 * control, plays once when the GIF has no NETSCAPE loop block, and its
 * software transform (LV_DRAW_SW_ASM_NONE, per-pixel ARGB8888 blending) cost
 * about 70 of the 105 ms a render of the 2x box took (hardware 2026-09-27/28).
 *
 * - LV_GIF_CACHE_DECODE_DATA=y is required (Kconfig selects it): in this LVGL
 *   checkout the other read_image_data() rejects every frame's last LZW token
 *   (`frm_off + str_len >= frm_size`, corrected to `>` upstream in 660b41df9),
 *   desynchronizes the stream and leaks its code table. The cached variant is
 *   a separate implementation, host-checked against Pillow's composited
 *   frames on 2026-09-27.
 * - gif_open() fills the canvas with the background colour at alpha 0xFF, and
 *   that stays visible wherever no frame paints; the canvas is cleared to
 *   transparent right after open and whenever gifdec seeks back to anim_start
 *   at the trailer (the only backwards move of its read pointer), so every
 *   pass starts transparent, as browsers and Pillow do. loop_count is 1 for
 *   the first decode, so that a GIF without any frame returns 0 at its
 *   trailer instead of making gd_get_frame() seek back forever, and 0 (loop
 *   forever) afterwards: read_application_ext() only overwrites a negative
 *   count, and a GIF without a NETSCAPE block would otherwise stop after one
 *   pass.
 * - Inherited from gifdec, not handled: disposal 3 (restore to previous) is a
 *   no-op, and a frame without its own GCE reuses the previous frame's delay,
 *   transparency and disposal. Such GIFs show trails or holes on the device.
 * - Timing: the GCE delay (10 ms units; 0 and 1 read as 100 ms, as browsers
 *   do) divided by the speed, kept in 1/100 ms so that 50 ms at 150 % is
 *   33.33 ms rather than 33. A TICK_MS lv_timer decodes every frame that is
 *   due, at most CATCHUP_MAX per tick (then drops the backlog), and
 *   invalidates at most once per tick and only when the canvas changed
 *   (FNV-1a over its words), so the tempo holds while drawing is slower than
 *   the GIF, as long as the frames due per tick stay within CATCHUP_MAX. The
 *   whole box is invalidated: the test GIF's frames cover 65-76 % of the
 *   canvas and consecutive ones nearly all of it, and invalidating only the
 *   touched rectangles changed nothing measurable (hardware 2026-09-28).
 * - Keystrokes (canon task t-7c05): while the keyboard is typed on (the
 *   observer's key_ms within KEY_GRACE_MS, status_observer.h) the tempo
 *   stops, and every press the observer counted owes the sprite
 *   FRAMES_PER_PRESS frames that change the canvas, each at most
 *   STEP_DECODE_MAX decodes (the test GIF alternates a painted frame with one
 *   that paints nothing). The frames due are stepped once per render: the
 *   tick steps only once the display has rendered the last step
 *   (LV_EVENT_RENDER_READY clears render_pending), one frame at a time while
 *   the queue holds one press's worth or less (so a lone press shows every
 *   frame), and as many as drain the queue in about DRAIN_RENDERS renders
 *   beyond that (so the sprite runs faster and skips frames as presses pile
 *   up), within DECODES_PER_TICK decodes a tick. The queue holds
 *   FRAMES_QUEUE_MAX frames; a burst beyond that (the observer already drops
 *   a keyboard reboot's counter restart above KEYS_DELTA_MAX) loses the
 *   rest. KEY_GRACE_MS after the last press arrived the frames still due are
 *   dropped and the tempo resumes from the shown frame: frame_at moves to
 *   the present on every typing tick, so nothing falls due meanwhile. With
 *   KEY_GRACE_MS equal to the advertising interval (the user's pick on
 *   hardware) that is the look: a lone press shows three or four of its
 *   frames before the tempo takes over, and while presses keep coming the
 *   queue never holds more than the presses of the last payload, so the
 *   sprite runs at the display's pace, skipping frames in proportion to how
 *   fast the keys come. gifdec only moves forward, so a press never steps
 *   back.
 * - Drawing: blit() runs on LV_EVENT_DRAW_MAIN of a plain, transparent object
 *   and writes RGB565 into the layer's buffer for the clip area. That is
 *   safe under three conditions, which the screen keeps: LVGL has no OS
 *   (LV_USE_OS == LV_OS_NONE, asserted below), so lv_draw_finalize_task_creation()
 *   dispatches and the sw unit renders each draw task synchronously, one per
 *   task created; the sprite is the screen's first child, so the only task
 *   before its event is the screen's fill, which has therefore landed; and
 *   nothing drawn before the sprite renders through a layer (opa_layered,
 *   transform, blend mode, bitmap mask, or an lv_bar indicator shorter than
 *   its radius), whose blend task is queued one task late and would land on
 *   top of the sprite. The objects after the sprite draw over it as usual.
 *   The refresh renders an invalid area in VDB-sized parts and sends the
 *   event once per part with layer->buf_area / _clip_area set to it. The
 *   layer holds native RGB565: LV_COLOR_16_SWAP is applied at flush
 *   (lv_refr.c, lv_draw_sw_rgb565_swap()); a layer of another format (LVGL
 *   would render the sprite into an ARGB8888 layer if it were ever layered
 *   itself) is left alone. The canvas is B, G, R, A per pixel (gifdec.c
 *   render_frame_rect()); GIFs have no partial alpha, so a pixel is drawn or
 *   skipped, and a skipped one shows the screen's black, the only thing under
 *   the sprite box.
 * - Display work queue only (LV_USE_OS=0): creation from
 *   zmk_display_status_screen(), the timer and the draw event.
 * - gd_get_frame() < 0 (malformed data) or no frame at all: the sprite is
 *   removed and its pool memory freed; the rest of the screen stays.
 */

#include <stdint.h>
#include <string.h>

#include <lvgl.h>
#include <libs/gif/gifdec.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#ifdef CONFIG_SYS_HEAP_RUNTIME_STATS
#include <lvgl_mem.h>
#endif

#include "sprite.h"
#include "status_observer.h"

LOG_MODULE_REGISTER(beacon_sprite, LOG_LEVEL_INF);

BUILD_ASSERT(sizeof(gd_GIF) % sizeof(uint32_t) == 0,
             "the canvas follows gd_GIF in one allocation and is read as words");
BUILD_ASSERT(LV_USE_OS == LV_OS_NONE,
             "blit() writes into the layer buffer directly; with an OS the screen's fill could land later");

#define TICK_MS 10
/* The tick cannot run while LVGL renders, so a cycle is one render plus one
 * decode per frame due, and the tempo ceiling is CATCHUP_MAX frames a cycle. A
 * higher limit raises the ceiling and costs renders. */
#define CATCHUP_MAX 8
/* The tempo resumes this long after the last key press arrived, the frames
 * still queued dropped: a pick of its own (the user's) that equals
 * BEACON_PAYLOAD_INTERVAL_MS (status_payload.h), so the queue never outlives
 * the presses of one payload and the sprite never stands still between the
 * last step and the tempo. */
#define KEY_GRACE_MS 200
/* Frames a key press owes: the user's pick (2026-09-29, after 1, 2 and 4). */
#define FRAMES_PER_PRESS 8
/* Frames owed to presses and not yet stepped, at most: four presses. */
#define FRAMES_QUEUE_MAX (4 * FRAMES_PER_PRESS)
/* A queue above one press's worth is drained in about this many renders
 * (each about 65 ms with the test GIF: 2 decodes and a render): two frames a
 * render from two presses, up to six from a full queue. */
#define DRAIN_RENDERS 6
/* Decodes per visible frame at most, looking for one that changes the canvas. */
#define STEP_DECODE_MAX 8
/* Decodes a typing tick may spend (about 90 ms): a frame stops at this bound
 * even before its canvas changed, and the display thread outranks BT RX. */
#define DECODES_PER_TICK 8
#define DELAY_UNIT_MS 10
#define DELAY_MIN_UNITS 10
#define SUB_PER_MS 100
#define SPEED_FULL_PCT 100
#define LOG_EVERY_MS 60000
/* gifdec.c LZW_CACHE_SIZE, for the log line. */
#define LZW_CACHE_BYTES 16384

static const uint8_t sprite_gif[] = {
#include <beacon_sprite_gif.inc>
};

static struct {
    gd_GIF *gif;
    lv_obj_t *obj;
    lv_timer_t *timer;
    uint16_t speed_pct;
    /* lv_tick in 1/SUB_PER_MS ms: when the shown frame became due. */
    uint32_t frame_at;
    /* The shown frame's delay at 100 %. */
    uint32_t frame_ms;
    /* The canvas as last drawn. */
    uint32_t hash;
    /* The observer's keystrokes count the sprite has caught up with. */
    uint32_t keys_seen;
    /* Visible frames owed to key presses, not yet stepped. */
    uint32_t frames_due;
    /* An invalidation the display has not rendered yet. */
    bool render_pending;
    uint32_t decoded;
    uint32_t stepped;
    uint32_t invalidated;
    uint32_t logged_at;
    /* Every screen render (the HP bar's included) and its time, flush included;
     * logging builds only. */
    uint32_t render_start;
    uint32_t renders;
    uint32_t render_us;
} sprite;

static uint32_t canvas_hash(void) {
    /* Word access: lv_malloc returns 8-aligned blocks and the canvas follows
     * gd_GIF, whose size is a multiple of 4 (BUILD_ASSERT above). */
    const uint32_t *px = (const uint32_t *)sprite.gif->canvas;
    const size_t n = (size_t)sprite.gif->width * sprite.gif->height;
    uint32_t h = 2166136261u;

    for (size_t i = 0; i < n; i++) {
        h = (h ^ px[i]) * 16777619u;
    }
    return h;
}

static bool decode_next(void) {
    gd_GIF *gif = sprite.gif;
    const uint32_t before = gif->f_rw_p;

    if (gd_get_frame(gif) <= 0) {
        return false;
    }
    if (gif->f_rw_p < before) {
        /* The trailer sent gifdec back to anim_start. */
        memset(gif->canvas, 0, 4u * gif->width * gif->height);
    }
    gd_render_frame(gif, gif->canvas);

    uint32_t units = gif->gce.delay;
    if (units < 2) {
        units = DELAY_MIN_UNITS;
    }
    sprite.frame_ms = units * DELAY_UNIT_MS;
    sprite.decoded++;
    return true;
}

/* One visible frame: the next that changes the canvas, within STEP_DECODE_MAX
 * decodes and the tick's budget (a GIF of identical frames stops at either
 * limit). Adds its decodes to *decodes. */
static bool step_frame(unsigned int *decodes, unsigned int budget) {
    const uint32_t from = canvas_hash();

    for (unsigned int i = 0; i < STEP_DECODE_MAX && *decodes < budget; i++) {
        if (!decode_next()) {
            return false;
        }
        (*decodes)++;
        if (canvas_hash() != from) {
            break;
        }
    }
    sprite.stepped++;
    return true;
}

static uint32_t interval_sub(void) {
    const uint64_t sub = (uint64_t)sprite.frame_ms * SUB_PER_MS * SPEED_FULL_PCT / sprite.speed_pct;
    return (uint32_t)MIN(sub, INT32_MAX);
}

static void remove_sprite(const char *why) {
    LOG_ERR("sprite removed: %s", why);
    lv_timer_delete(sprite.timer);
    sprite.timer = NULL;
    lv_obj_delete(sprite.obj);
    sprite.obj = NULL;
    gd_close_gif(sprite.gif);
    sprite.gif = NULL;
}

static void log_stats(void) {
#ifdef CONFIG_SYS_HEAP_RUNTIME_STATS
    struct sys_memory_stats heap;

    lvgl_heap_stats(&heap);
    LOG_INF("sprite %u decoded, %u key steps, %u invalidated, %u renders of %u ms in %d s, "
            "speed %u%%, lvgl pool %u allocated, %u max, of %d",
            sprite.decoded, sprite.stepped, sprite.invalidated, sprite.renders,
            sprite.renders ? sprite.render_us / sprite.renders / 1000 : 0, LOG_EVERY_MS / 1000,
            sprite.speed_pct, (unsigned int)heap.allocated_bytes,
            (unsigned int)heap.max_allocated_bytes, CONFIG_LV_Z_MEM_POOL_SIZE);
#else
    LOG_INF("sprite %u decoded, %u key steps, %u invalidated, %u renders of %u ms in %d s, "
            "speed %u%%",
            sprite.decoded, sprite.stepped, sprite.invalidated, sprite.renders,
            sprite.renders ? sprite.render_us / sprite.renders / 1000 : 0, LOG_EVERY_MS / 1000,
            sprite.speed_pct);
#endif
    sprite.decoded = 0;
    sprite.stepped = 0;
    sprite.invalidated = 0;
    sprite.renders = 0;
    sprite.render_us = 0;
}

static void tick(lv_timer_t *timer) {
    ARG_UNUSED(timer);
    const uint32_t now = lv_tick_get();
    const uint32_t now_sub = now * SUB_PER_MS;
    struct beacon_status status;
    unsigned int n = 0;

    beacon_status_get(&status);
    /* key_ms is 0 until the first press arrives. */
    const bool typing = status.key_ms != 0 && k_uptime_get() - status.key_ms < KEY_GRACE_MS;

    if (typing) {
        const uint32_t presses = status.keystrokes - sprite.keys_seen;

        sprite.keys_seen = status.keystrokes;
        sprite.frames_due = MIN(sprite.frames_due + presses * FRAMES_PER_PRESS, FRAMES_QUEUE_MAX);
        if (sprite.frames_due > 0 && !sprite.render_pending) {
            uint32_t want = sprite.frames_due > FRAMES_PER_PRESS
                                ? DIV_ROUND_UP(sprite.frames_due, DRAIN_RENDERS)
                                : 1;
            unsigned int decodes = 0;

            while (want > 0 && decodes < DECODES_PER_TICK) {
                if (!step_frame(&decodes, DECODES_PER_TICK)) {
                    remove_sprite("malformed GIF data");
                    return;
                }
                sprite.frames_due--;
                want--;
                n++;
            }
        }
        sprite.frame_at = now_sub;
    } else {
        for (;;) {
            const uint32_t interval = interval_sub();
            if ((int32_t)(now_sub - sprite.frame_at) < (int32_t)interval) {
                break;
            }
            if (n == CATCHUP_MAX) {
                sprite.frame_at = now_sub;
                break;
            }
            sprite.frame_at += interval;
            if (!decode_next()) {
                remove_sprite("malformed GIF data");
                return;
            }
            n++;
        }
        sprite.keys_seen = status.keystrokes;
        sprite.frames_due = 0;
    }

    if (n > 0) {
        const uint32_t hash = canvas_hash();
        if (hash != sprite.hash) {
            sprite.hash = hash;
            sprite.invalidated++;
            sprite.render_pending = true;
            lv_obj_invalidate(sprite.obj);
        }
    }

    if (IS_ENABLED(CONFIG_LOG) && now - sprite.logged_at >= LOG_EVERY_MS) {
        sprite.logged_at = now;
        log_stats();
    }
}

/* The canvas, nearest neighbour, into the layer's RGB565 buffer: destination
 * column x reads source column (x - x1) * src_w / obj_w, stepped as a DDA
 * (src_w <= obj_w, so at most one source column per destination pixel). */
static void blit(lv_event_t *e) {
    lv_layer_t *layer = lv_event_get_layer(e);
    const lv_draw_buf_t *buf = layer->draw_buf;
    lv_area_t coords;
    lv_obj_get_coords(lv_event_get_target(e), &coords);

    const int32_t x1 = MAX(coords.x1, layer->_clip_area.x1);
    const int32_t y1 = MAX(coords.y1, layer->_clip_area.y1);
    const int32_t x2 = MIN(coords.x2, layer->_clip_area.x2);
    const int32_t y2 = MIN(coords.y2, layer->_clip_area.y2);
    if (x1 > x2 || y1 > y2) {
        return;
    }
    if (layer->color_format != LV_COLOR_FORMAT_RGB565) {
        static bool said;
        if (!said) {
            said = true;
            LOG_ERR("sprite: layer format %d is not RGB565, nothing drawn", layer->color_format);
        }
        return;
    }

    const gd_GIF *gif = sprite.gif;
    /* Word access as in canvas_hash(): B | G << 8 | R << 16 | A << 24. */
    const uint32_t *canvas = (const uint32_t *)gif->canvas;
    const int32_t src_w = gif->width;
    const int32_t obj_w = lv_area_get_width(&coords);
    const int32_t obj_h = lv_area_get_height(&coords);
    const int32_t x_off = x1 - coords.x1;
    const int32_t sx0 = x_off * src_w / obj_w;
    const int32_t err0 = x_off * src_w - sx0 * obj_w;

    for (int32_t y = y1; y <= y2; y++) {
        const int32_t sy = (y - coords.y1) * gif->height / obj_h;
        const uint32_t *srow = canvas + (size_t)sy * src_w;
        uint16_t *dst = (uint16_t *)(buf->data + (size_t)(y - layer->buf_area.y1) * buf->header.stride) +
                        (x1 - layer->buf_area.x1);
        int32_t sx = sx0;
        int32_t err = err0;

        for (int32_t x = x1; x <= x2; x++, dst++) {
            const uint32_t px = srow[sx];
            if (px >> 24) {
                *dst = (uint16_t)(((px >> 8) & 0xF800) | ((px >> 5) & 0x07E0) | ((px >> 3) & 0x001F));
            }
            err += src_w;
            if (err >= obj_w) {
                err -= obj_w;
                sx++;
            }
        }
    }
}

/* Every refresh renders all invalid areas, the sprite's included, so a
 * RENDER_READY means the last invalidated canvas is on the panel. */
static void render_ready(lv_event_t *e) {
    ARG_UNUSED(e);
    sprite.render_pending = false;
}

static void count_render(lv_event_t *e) {
    const uint32_t now = k_cycle_get_32();

    if (lv_event_get_code(e) == LV_EVENT_RENDER_START) {
        sprite.render_start = now;
    } else {
        sprite.renders++;
        sprite.render_us += k_cyc_to_us_floor32(now - sprite.render_start);
    }
}

void beacon_sprite_create(lv_obj_t *parent, const lv_area_t *box, uint16_t speed_pct) {
    const int32_t box_w = lv_area_get_width(box);
    const int32_t box_h = lv_area_get_height(box);

    gd_GIF *gif = gd_open_gif_data(sprite_gif);
    if (gif == NULL) {
        LOG_ERR("sprite: GIF rejected or no pool memory for it (%u byte file)",
                (unsigned int)sizeof(sprite_gif));
        return;
    }
    /* The largest size of the GIF's proportions inside the box, whole factor
     * or not (a GIF pixel then covers two or three panel pixels in turn). */
    int32_t w, h;
    if ((int64_t)box_w * gif->height <= (int64_t)box_h * gif->width) {
        w = box_w;
        h = (int32_t)((int64_t)box_w * gif->height / gif->width);
    } else {
        h = box_h;
        w = (int32_t)((int64_t)box_h * gif->width / gif->height);
    }
    if (w < gif->width || h < gif->height) {
        LOG_ERR("sprite %ux%u does not fit %dx%d", gif->width, gif->height, box_w, box_h);
        gd_close_gif(gif);
        return;
    }

    memset(gif->canvas, 0, 4u * gif->width * gif->height);
    gif->loop_count = 1;
    sprite.gif = gif;
    if (!decode_next()) {
        LOG_ERR("sprite: no frame, or the first frame is malformed");
        gd_close_gif(gif);
        sprite.gif = NULL;
        return;
    }
    gif->loop_count = 0;
    sprite.hash = canvas_hash();

    /* A plain object that draws nothing of its own; blit() paints its area.
     * LVGL's defaults already draw nothing, and the explicit styles keep it
     * so should a theme ever be installed. */
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_outline_width(obj, 0, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_pos(obj, box->x1 + (box_w - w) / 2, box->y1 + box_h - h);
    lv_obj_add_event_cb(obj, blit, LV_EVENT_DRAW_MAIN, NULL);

    struct beacon_status status;
    beacon_status_get(&status);

    sprite.obj = obj;
    sprite.speed_pct = MAX(speed_pct, 1);
    sprite.keys_seen = status.keystrokes;
    sprite.frame_at = lv_tick_get() * SUB_PER_MS;
    sprite.logged_at = lv_tick_get();
    sprite.timer = lv_timer_create(tick, TICK_MS, NULL);
    lv_display_t *disp = lv_obj_get_display(obj);
    lv_display_add_event_cb(disp, render_ready, LV_EVENT_RENDER_READY, NULL);
    if (IS_ENABLED(CONFIG_LOG)) {
        lv_display_add_event_cb(disp, count_render, LV_EVENT_RENDER_START, NULL);
        lv_display_add_event_cb(disp, count_render, LV_EVENT_RENDER_READY, NULL);
    }

    LOG_INF("sprite %ux%u as %dx%d (%d.%02dx) from a %u byte GIF, %u bytes of the lvgl pool",
            gif->width, gif->height, w, h, h / gif->height, h * 100 / gif->height % 100,
            (unsigned int)sizeof(sprite_gif),
            (unsigned int)(sizeof(gd_GIF) + 5u * gif->width * gif->height + LZW_CACHE_BYTES));
}
