/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * The status screen's GIF sprite: the file named by CONFIG_BEACON_SPRITE_GIF,
 * embedded at build time (CMakeLists.txt), decoded with LVGL's gifdec into an
 * ARGB8888 canvas that an lv_image shows scaled by an integer factor. An own
 * player instead of lv_gif: the stock widget invalidates on every frame even
 * when the canvas did not change (half the frames of the test GIF, host check
 * 2026-09-27), has no speed control, and plays once when the GIF has no
 * NETSCAPE loop block.
 *
 * - LV_GIF_CACHE_DECODE_DATA=y is required (Kconfig selects it): in this LVGL
 *   checkout the other read_image_data() rejects every frame's last LZW token
 *   (`frm_off + str_len >= frm_size`, corrected to `>` upstream in 660b41df9),
 *   desynchronizes the stream and leaks its code table. The cached variant is
 *   a separate implementation, host-checked against Pillow's composited
 *   frames on 2026-09-27.
 * - gif_open() fills the canvas with the background colour at alpha 0xFF, and
 *   that stays visible wherever no frame paints; the canvas is cleared to
 *   transparent right after open. loop_count is 1 for the first decode, so
 *   that a GIF without any frame returns 0 at its trailer instead of making
 *   gd_get_frame() seek back to anim_start forever, and 0 (loop forever)
 *   afterwards: read_application_ext() only overwrites a negative count, and a
 *   GIF without a NETSCAPE block would otherwise stop after one pass.
 * - Inherited from gifdec, not handled: disposal 3 (restore to previous) is a
 *   no-op, and a frame without its own GCE reuses the previous frame's delay,
 *   transparency and disposal. Such GIFs show trails or holes on the device.
 * - Extra draw area: STRETCH scales around pivot (0,0), so the image draws
 *   exactly inside the object's coords, but lv_image's
 *   LV_EVENT_REFR_EXT_DRAW_SIZE handler transforms the already stretched size
 *   a second time and claims about one object size on every side. Every
 *   changed frame then redrew the whole 280x240 panel (hardware 2026-09-27:
 *   about 67,000 px and 152 ms a frame, 5 frames a second). no_ext_draw()
 *   runs after the class handler and takes the claim back.
 * - Timing: the GCE delay (10 ms units; 0 and 1 read as 100 ms, as browsers
 *   do) divided by the speed, kept in 1/100 ms so that 50 ms at 300 % is
 *   16.67 ms rather than 16. A TICK_MS lv_timer decodes every frame that is
 *   due, at most CATCHUP_MAX per tick (then drops the backlog), and
 *   invalidates at most once per tick and only when the canvas changed
 *   (FNV-1a over its words), so the tempo holds while drawing is slower than
 *   the GIF.
 * - Display work queue only (LV_USE_OS=0): creation from
 *   zmk_display_status_screen(), the timer, and beacon_sprite_set_speed()
 *   from the screen's refresh timer.
 * - gd_get_frame() < 0 (malformed data) or no frame at all: the sprite is
 *   removed and its pool memory freed; the battery labels are unaffected.
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

LOG_MODULE_REGISTER(beacon_sprite, LOG_LEVEL_INF);

BUILD_ASSERT(sizeof(gd_GIF) % sizeof(uint32_t) == 0,
             "the canvas follows gd_GIF in one allocation and is hashed as words");

#define TICK_MS 10
#define CATCHUP_MAX 8
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
    lv_obj_t *img;
    lv_image_dsc_t dsc;
    lv_timer_t *timer;
    uint16_t speed_pct;
    /* lv_tick in 1/SUB_PER_MS ms: when the shown frame became due. */
    uint32_t frame_at;
    /* The shown frame's delay at 100 %. */
    uint32_t frame_ms;
    /* The canvas as last drawn. */
    uint32_t hash;
    uint32_t decoded;
    uint32_t invalidated;
    uint32_t logged_at;
    /* Every screen render (labels included) and its time, flush included;
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
        /* The trailer sent gifdec back to anim_start (the only backwards
         * move of its read pointer): every pass starts on a transparent
         * canvas, as browsers and Pillow do. gifdec alone leaves the last
         * frame's pixels under the first frames (32 px for two frames with
         * the test GIF, host check 2026-09-27). */
        memset(gif->canvas, 0, 4u * gif->width * gif->height);
    }
    gd_render_frame(gif, gif->canvas);

    uint32_t units = sprite.gif->gce.delay;
    if (units < 2) {
        units = DELAY_MIN_UNITS;
    }
    sprite.frame_ms = units * DELAY_UNIT_MS;
    sprite.decoded++;
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
    lv_obj_delete(sprite.img);
    sprite.img = NULL;
    gd_close_gif(sprite.gif);
    sprite.gif = NULL;
}

static void log_stats(void) {
#ifdef CONFIG_SYS_HEAP_RUNTIME_STATS
    struct sys_memory_stats heap;

    lvgl_heap_stats(&heap);
    LOG_INF("sprite %u decoded, %u invalidated, %u renders of %u ms in %d s, speed %u%%, "
            "lvgl pool %u allocated, %u max, of %d",
            sprite.decoded, sprite.invalidated, sprite.renders,
            sprite.renders ? sprite.render_us / sprite.renders / 1000 : 0, LOG_EVERY_MS / 1000,
            sprite.speed_pct, (unsigned int)heap.allocated_bytes,
            (unsigned int)heap.max_allocated_bytes, CONFIG_LV_Z_MEM_POOL_SIZE);
#else
    LOG_INF("sprite %u decoded, %u invalidated, %u renders of %u ms in %d s, speed %u%%",
            sprite.decoded, sprite.invalidated, sprite.renders,
            sprite.renders ? sprite.render_us / sprite.renders / 1000 : 0, LOG_EVERY_MS / 1000,
            sprite.speed_pct);
#endif
    sprite.decoded = 0;
    sprite.invalidated = 0;
    sprite.renders = 0;
    sprite.render_us = 0;
}

static void tick(lv_timer_t *timer) {
    ARG_UNUSED(timer);
    const uint32_t now = lv_tick_get();

    if (sprite.speed_pct > 0) {
        const uint32_t now_sub = now * SUB_PER_MS;
        unsigned int n = 0;

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

        if (n > 0) {
            const uint32_t hash = canvas_hash();
            if (hash != sprite.hash) {
                sprite.hash = hash;
                sprite.invalidated++;
                /* A no-op with LV_CACHE_DEF_SIZE=0; a consumer's image cache
                 * would otherwise keep drawing the previous frame. */
                lv_image_cache_drop(&sprite.dsc);
                lv_obj_invalidate(sprite.img);
            }
        }
    }

    if (IS_ENABLED(CONFIG_LOG) && now - sprite.logged_at >= LOG_EVERY_MS) {
        sprite.logged_at = now;
        log_stats();
    }
}

void beacon_sprite_set_speed(uint16_t percent) {
    if (sprite.gif == NULL || percent == sprite.speed_pct) {
        return;
    }
    if (sprite.speed_pct == 0) {
        /* Resumed: the shown frame's delay starts now, not when it was paused. */
        sprite.frame_at = lv_tick_get() * SUB_PER_MS;
    }
    sprite.speed_pct = percent;
}

static void no_ext_draw(lv_event_t *e) {
    *(int32_t *)lv_event_get_param(e) = 0;
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

lv_obj_t *beacon_sprite_create(lv_obj_t *parent, const lv_area_t *box) {
    const int32_t box_w = lv_area_get_width(box);
    const int32_t box_h = lv_area_get_height(box);

    gd_GIF *gif = gd_open_gif_data(sprite_gif);
    if (gif == NULL) {
        LOG_ERR("sprite: GIF rejected or no pool memory for it (%u byte file)",
                (unsigned int)sizeof(sprite_gif));
        return NULL;
    }
    const int32_t scale = MIN(box_w / gif->width, box_h / gif->height);
    if (scale < 1) {
        LOG_ERR("sprite %ux%u does not fit %dx%d", gif->width, gif->height, box_w, box_h);
        gd_close_gif(gif);
        return NULL;
    }

    const uint32_t canvas_bytes = 4u * gif->width * gif->height;
    memset(gif->canvas, 0, canvas_bytes);
    gif->loop_count = 1;
    sprite.gif = gif;
    sprite.dsc = (lv_image_dsc_t){
        .header =
            {
                .magic = LV_IMAGE_HEADER_MAGIC,
                .cf = LV_COLOR_FORMAT_ARGB8888,
                .flags = LV_IMAGE_FLAGS_MODIFIABLE,
                .w = gif->width,
                .h = gif->height,
                .stride = gif->width * 4,
            },
        .data_size = canvas_bytes,
        .data = gif->canvas,
    };
    if (!decode_next()) {
        LOG_ERR("sprite: no frame, or the first frame is malformed");
        gd_close_gif(gif);
        sprite.gif = NULL;
        return NULL;
    }
    gif->loop_count = 0;
    sprite.hash = canvas_hash();

    lv_obj_t *img = lv_image_create(parent);
    lv_obj_add_event_cb(img, no_ext_draw, LV_EVENT_REFR_EXT_DRAW_SIZE, NULL);
    lv_image_set_src(img, &sprite.dsc);
    /* src, then size, then STRETCH: lv_image derives the scale from the
     * object size in set_src() and set_inner_align() only, not on a resize. */
    lv_obj_set_size(img, gif->width * scale, gif->height * scale);
    lv_obj_set_pos(img, box->x1 + (box_w - gif->width * scale) / 2, box->y1);
    lv_image_set_antialias(img, false);
    lv_image_set_inner_align(img, LV_IMAGE_ALIGN_STRETCH);

    sprite.img = img;
    sprite.speed_pct = 0;
    sprite.frame_at = lv_tick_get() * SUB_PER_MS;
    sprite.logged_at = lv_tick_get();
    sprite.timer = lv_timer_create(tick, TICK_MS, NULL);
    if (IS_ENABLED(CONFIG_LOG)) {
        lv_display_t *disp = lv_obj_get_display(img);
        lv_display_add_event_cb(disp, count_render, LV_EVENT_RENDER_START, NULL);
        lv_display_add_event_cb(disp, count_render, LV_EVENT_RENDER_READY, NULL);
    }

    LOG_INF("sprite %ux%u at %dx from a %u byte GIF, %u bytes of the lvgl pool", gif->width,
            gif->height, (int)scale, (unsigned int)sizeof(sprite_gif),
            (unsigned int)(sizeof(gd_GIF) + 5u * gif->width * gif->height + LZW_CACHE_BYTES));
    return img;
}
