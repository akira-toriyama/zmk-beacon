/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * The status screen's sprite: a sprite pack (sprite_pack.h; the GIF named by
 * CONFIG_BEACON_SPRITE_GIF is converted at build time, CMakeLists.txt),
 * decoded picture by picture into a packed canvas and drawn by blit() straight
 * into the display buffer, scaled by nearest neighbour to fill its box. An own
 * player and an own draw rather than lv_gif and lv_image: the widget
 * invalidates on every frame even when nothing changed, has no tempo control,
 * and its software transform cost about 70 of the 105 ms a render of the 2x
 * box took (hardware 2026-09-27/28); and an own format rather than the GIF
 * (docs/sprite-pack.md): no decode to move between frames, the repeated
 * pictures stored once, and 2 bytes a pixel of the pool instead of 5 plus a
 * 16 KiB LZW cache.
 *
 * - The pack is memory that stays readable and unchanged while shown: the
 *   embedded array. The pool holds one block for the animation with the
 *   largest canvas in the file: that canvas (stride x height bytes, 4 or 8
 *   bits a pixel) and the decoded picture 0, the dictionary every other
 *   picture inflates against. The front animation plays; a back one in the
 *   file waits for its switch.
 * - Every frame names a picture, so the player never decodes to move: the
 *   tempo advances the frame index for the time elapsed and decodes once,
 *   the frame that is due now, when its picture is not the canvas's. A frame
 *   shows for its delay (10 ms units; the converter already raised 0 and 1
 *   to 10, as browsers do) at SPEED_PCT, counted in 1/SUB_PER_MS ms so that
 *   a fraction of a millisecond is not lost on every frame. Every TICK_MS the
 *   frames due are stepped, CATCHUP_MAX at most (the backlog beyond is
 *   dropped), and the box is invalidated once when the picture changed.
 * - Key presses (status_observer.h keystrokes and key_ms): while the last
 *   press arrived less than KEY_GRACE_MS ago the tempo stops, and each press
 *   queues FRAMES_PER_PRESS frames that change the picture, FRAMES_QUEUE_MAX
 *   in all. The queue steps only once LV_EVENT_RENDER_READY has cleared
 *   render_pending: a frame stepped before the display drew the last one is
 *   never seen. A step is one frame while the queue holds a press's worth or
 *   less, so a lone press shows its frames one by one, and enough to drain
 *   the queue in DRAIN_RENDERS renders beyond that, so the sprite runs faster
 *   and skips frames as presses pile up. When the grace expires the frames
 *   still queued are dropped and the tempo resumes from the shown frame.
 * - Screen off (screen_off.h): while the screen is dark nothing is decoded.
 *   The tempo stands still and resumes from the shown frame; a press queues
 *   its frames without stepping.
 * - Drawing: blit() runs on LV_EVENT_DRAW_MAIN of a plain, transparent object
 *   and writes RGB565 into the layer's buffer for the clip area, under the
 *   same three conditions as before: no LVGL OS (asserted), the sprite is the
 *   screen's first child, and the screen renders through no layer. A pixel's
 *   value indexes lut (RGB565, built from the palette); 0 is skipped and
 *   shows the screen's black.
 * - Display work queue only (LV_USE_OS=0): creation, the timer and the draw
 *   event.
 * - A picture that does not inflate: the sprite is removed and its pool
 *   memory freed; the rest of the screen stays.
 */

#include <stdint.h>
#include <string.h>

#include <lvgl.h>
#include <lvgl_mem.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#if IS_ENABLED(CONFIG_BEACON_SCREEN_OFF)
#include "screen_off.h"
#endif
#include "sprite.h"
#include "sprite_pack.h"
#include "status_observer.h"

LOG_MODULE_REGISTER(beacon_sprite, LOG_LEVEL_INF);

BUILD_ASSERT(LV_USE_OS == LV_OS_NONE,
             "blit() writes into the layer buffer directly; with an OS the screen's fill could land later");

#define TICK_MS 10
/* Tempo while nobody types, in percent of the GIF's own: the user's pick. */
#define SPEED_PCT 75
/* Frames a tick advances at most: moving costs no decode, so this only
 * bounds the loop after a long stall. */
#define CATCHUP_MAX 256
/* The tempo resumes this long after the last key press arrived, the frames
 * still queued dropped: a pick of its own (the user's) that equals
 * BEACON_PAYLOAD_INTERVAL_MS (status_payload.h), so the queue never outlives
 * the presses of one payload and the sprite never stands still between the
 * last step and the tempo. */
#define KEY_GRACE_MS 200
/* Frames a key press queues: the user's pick. */
#define FRAMES_PER_PRESS 8
/* Frames queued at most, four presses' worth; a burst beyond loses the rest. */
#define FRAMES_QUEUE_MAX (4 * FRAMES_PER_PRESS)
/* A queue above one press's worth drains in about this many renders (a render
 * took 27-51 ms with the pack, hardware 2026-10-06). */
#define DRAIN_RENDERS 6
/* The pack's delay unit; the converter already turned 0 and 1 into 10. */
#define DELAY_UNIT_MS 10
#define SUB_PER_MS 100
#define SPEED_FULL_PCT 100
#define LOG_PERIOD_MS 60000

static const uint8_t sprite_embedded[] = {
#include <beacon_sprite_pack.inc>
};

static struct {
    const uint8_t *file;
    struct sprite_pack_anim anim;
    uint8_t side;
    uint8_t sides;
    /* One pool block: the canvas, then the dictionary, `buffer` bytes each. */
    uint8_t *canvas;
    uint8_t *dict;
    size_t buffer;
    uint16_t lut[256];
    /* The frame the tempo is at, the picture in the canvas, and the picture
     * last invalidated for. */
    uint16_t frame;
    uint16_t picture;
    uint16_t shown;
    /* The current frame's delay at 100 %. */
    uint32_t frame_ms;
    lv_area_t box;
    lv_obj_t *obj;
    lv_timer_t *timer;
    /* lv_tick in 1/SUB_PER_MS ms: when the current frame became due. */
    uint32_t frame_at;
    uint32_t keys_seen;
    uint32_t frames_due;
    bool render_pending;
    uint32_t decoded;
    uint32_t decode_us;
    uint32_t decode_max_us;
    uint32_t presses;
    uint32_t stepped;
    uint32_t dropped;
    uint32_t invalidated;
    uint32_t logged_at;
    uint32_t render_start;
    uint32_t renders;
    uint32_t render_us;
} sprite;

static uint32_t cycles_to_us(uint32_t cycles) { return k_cyc_to_us_floor32(cycles); }

/* Picture `index` into the canvas (picture 0 is the dictionary: a copy). */
static bool load_picture(uint16_t index) {
    if (index == sprite.picture) {
        return true;
    }
    const uint32_t start = k_cycle_get_32();
    int err = 0;

    if (index == 0) {
        memcpy(sprite.canvas, sprite.dict, (size_t)sprite.anim.stride * sprite.anim.height);
    } else {
        err = sprite_pack_decode(&sprite.anim, index, sprite.canvas, sprite.dict);
    }
    const uint32_t us = cycles_to_us(k_cycle_get_32() - start);

    sprite.decoded++;
    sprite.decode_us += us;
    sprite.decode_max_us = MAX(sprite.decode_max_us, us);
    if (err != 0) {
        LOG_ERR("picture %u does not inflate (%d)", index, err);
        return false;
    }
    sprite.picture = index;
    return true;
}

static void set_frame(uint16_t frame) {
    uint16_t picture, delay;

    sprite.frame = frame;
    sprite_pack_frame(&sprite.anim, frame, &picture, &delay);
    sprite.frame_ms = (uint32_t)delay * DELAY_UNIT_MS;
}

static uint16_t frame_picture(uint16_t frame) {
    uint16_t picture, delay;

    sprite_pack_frame(&sprite.anim, frame, &picture, &delay);
    return picture;
}

static void advance(void) { set_frame((sprite.frame + 1) % sprite.anim.frames); }

/* One visible frame: the next whose picture is not the canvas's, within one
 * pass (an animation of one picture stops there). */
static void step_frame(void) {
    for (uint16_t i = 0; i < sprite.anim.frames; i++) {
        advance();
        if (frame_picture(sprite.frame) != sprite.picture) {
            break;
        }
    }
    sprite.stepped++;
}

static uint32_t interval_sub(void) {
    const uint64_t sub = (uint64_t)sprite.frame_ms * SUB_PER_MS * SPEED_FULL_PCT / SPEED_PCT;
    return (uint32_t)MIN(sub, INT32_MAX);
}

static void free_buffers(void) {
    if (sprite.canvas != NULL) {
        lv_free(sprite.canvas);
        sprite.canvas = NULL;
        sprite.dict = NULL;
        sprite.buffer = 0;
    }
}

static void remove_sprite(const char *why) {
    LOG_ERR("sprite removed: %s", why);
    lv_timer_delete(sprite.timer);
    sprite.timer = NULL;
    lv_obj_delete(sprite.obj);
    sprite.obj = NULL;
    free_buffers();
    sprite.file = NULL;
}

/* Logging builds only: BEACON_SPRITE selects SYS_HEAP_RUNTIME_STATS with LOG. */
static size_t pool_allocated(void) {
    struct sys_memory_stats heap;

    lvgl_heap_stats(&heap);
    return heap.allocated_bytes;
}

static void log_stats(void) {
    struct sys_memory_stats heap;

    lvgl_heap_stats(&heap);
    LOG_INF("sprite %u decoded (%u us avg, %u us max), %u presses for %u key steps and %u frames "
            "dropped, %u invalidated, %u renders of %u ms in %d s, speed %u%%, side %u, "
            "lvgl pool %u allocated, %u max, of %d",
            sprite.decoded, sprite.decoded ? sprite.decode_us / sprite.decoded : 0,
            sprite.decode_max_us, sprite.presses, sprite.stepped, sprite.dropped,
            sprite.invalidated, sprite.renders,
            sprite.renders ? sprite.render_us / sprite.renders / 1000 : 0, LOG_PERIOD_MS / 1000,
            SPEED_PCT, sprite.side, (unsigned int)heap.allocated_bytes,
            (unsigned int)heap.max_allocated_bytes, CONFIG_LV_Z_MEM_POOL_SIZE);
    sprite.decoded = 0;
    sprite.decode_us = 0;
    sprite.decode_max_us = 0;
    sprite.presses = 0;
    sprite.stepped = 0;
    sprite.dropped = 0;
    sprite.invalidated = 0;
    sprite.renders = 0;
    sprite.render_us = 0;
}

static bool screen_dark(void) {
#if IS_ENABLED(CONFIG_BEACON_SCREEN_OFF)
    return beacon_screen_is_off();
#else
    return false;
#endif
}

static void tick(lv_timer_t *timer) {
    ARG_UNUSED(timer);
    const uint32_t now = lv_tick_get();
    const uint32_t now_sub = now * SUB_PER_MS;
    const bool dark = screen_dark();
    struct beacon_status status;
    unsigned int n = 0;

    if (IS_ENABLED(CONFIG_LOG) && now - sprite.logged_at >= LOG_PERIOD_MS) {
        sprite.logged_at = now;
        log_stats();
    }

    beacon_status_get(&status);
    /* key_ms is 0 until the first press arrives. */
    const bool typing = status.key_ms != 0 && k_uptime_get() - status.key_ms < KEY_GRACE_MS;

    const uint32_t presses = status.keystrokes - sprite.keys_seen;

    sprite.keys_seen = status.keystrokes;
    sprite.presses += presses;
    if (typing) {
        const uint32_t owed = sprite.frames_due + presses * FRAMES_PER_PRESS;

        sprite.frames_due = MIN(owed, FRAMES_QUEUE_MAX);
        sprite.dropped += owed - sprite.frames_due;
        if (sprite.frames_due > 0 && !sprite.render_pending && !dark) {
            uint32_t want = sprite.frames_due > FRAMES_PER_PRESS
                                ? DIV_ROUND_UP(sprite.frames_due, DRAIN_RENDERS)
                                : 1;

            while (want > 0) {
                step_frame();
                sprite.frames_due--;
                want--;
                n++;
            }
        }
        sprite.frame_at = now_sub;
    } else {
        if (dark) {
            sprite.frame_at = now_sub;
        }
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
            advance();
            n++;
        }
        /* The grace expired: what is still queued is dropped, and presses
         * that arrived and went stale between two ticks queue nothing. */
        sprite.dropped += sprite.frames_due + presses * FRAMES_PER_PRESS;
        sprite.frames_due = 0;
    }

    if (n > 0 && !dark) {
        if (!load_picture(frame_picture(sprite.frame))) {
            remove_sprite("malformed picture data");
            return;
        }
        if (sprite.picture != sprite.shown) {
            sprite.shown = sprite.picture;
            sprite.invalidated++;
            sprite.render_pending = true;
            lv_obj_invalidate(sprite.obj);
        }
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

    const struct sprite_pack_anim *a = &sprite.anim;
    const uint16_t *lut = sprite.lut;
    const int32_t src_w = a->width;
    const int32_t obj_w = lv_area_get_width(&coords);
    const int32_t obj_h = lv_area_get_height(&coords);
    const int32_t x_off = x1 - coords.x1;
    const int32_t sx0 = x_off * src_w / obj_w;
    const int32_t err0 = x_off * src_w - sx0 * obj_w;

    for (int32_t y = y1; y <= y2; y++) {
        const int32_t sy = (y - coords.y1) * a->height / obj_h;
        const uint8_t *srow = sprite.canvas + (size_t)sy * a->stride;
        uint16_t *dst = (uint16_t *)(buf->data + (size_t)(y - layer->buf_area.y1) * buf->header.stride) +
                        (x1 - layer->buf_area.x1);
        int32_t sx = sx0;
        int32_t err = err0;

        if (a->bpp == 4) {
            for (int32_t x = x1; x <= x2; x++, dst++) {
                const uint8_t v = (srow[sx >> 1] >> ((~sx & 1) << 2)) & 0x0f;
                if (v) {
                    *dst = lut[v];
                }
                err += src_w;
                if (err >= obj_w) {
                    err -= obj_w;
                    sx++;
                }
            }
        } else {
            for (int32_t x = x1; x <= x2; x++, dst++) {
                const uint8_t v = srow[sx];
                if (v) {
                    *dst = lut[v];
                }
                err += src_w;
                if (err >= obj_w) {
                    err -= obj_w;
                    sx++;
                }
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
        sprite.render_us += cycles_to_us(now - sprite.render_start);
    }
}

/* Animation `side` of the open file into the block: palette, dictionary and
 * the first frame's picture. */
static bool set_side(uint8_t side) {
    struct sprite_pack_anim *a = &sprite.anim;

    if (!sprite_pack_open(sprite.file, side, a)) {
        return false;
    }
    sprite.side = side;
    memset(sprite.lut, 0, sizeof(sprite.lut));
    for (unsigned int i = 0; i < a->colours; i++) {
        const uint8_t *rgb = a->palette + 3 * i;
        sprite.lut[i + 1] = (uint16_t)((rgb[0] & 0xf8) << 8 | (rgb[1] & 0xfc) << 3 | rgb[2] >> 3);
    }
    const uint32_t start = k_cycle_get_32();
    if (sprite_pack_decode(a, 0, sprite.dict, NULL) != 0) {
        return false;
    }
    const uint32_t dict_us = cycles_to_us(k_cycle_get_32() - start);

    sprite.picture = UINT16_MAX;
    sprite.shown = UINT16_MAX;
    set_frame(0);
    if (!load_picture(frame_picture(0))) {
        return false;
    }
    LOG_INF("sprite %ux%u, %u bpp, %u colours, %u pictures, %u frames, side %u of %u, "
            "dictionary in %u us, %u bytes of the lvgl pool for canvas and dictionary",
            a->width, a->height, a->bpp, a->colours, a->pictures, a->frames, side, sprite.sides,
            dict_us, (unsigned int)(2 * sprite.buffer));
    return true;
}

/* Opens file as sprite.file with its animation `side` decoded; every
 * animation in the file must fit sprite.box at 1x: blit() never scales down. */
static bool open_file(const uint8_t *file, size_t avail, uint8_t side) {
    uint32_t total;
    const int sides = sprite_pack_count(file, avail, &total);
    size_t buffer = 0;

    if (sides <= 0 || side >= sides) {
        return false;
    }
    for (int i = 0; i < sides; i++) {
        struct sprite_pack_anim a;

        if (!sprite_pack_open(file, i, &a) || a.width > lv_area_get_width(&sprite.box) ||
            a.height > lv_area_get_height(&sprite.box)) {
            return false;
        }
        buffer = MAX(buffer, (size_t)a.stride * a.height);
    }
    free_buffers();
    sprite.canvas = lv_malloc(2 * buffer);
    if (sprite.canvas == NULL) {
        return false;
    }
    sprite.dict = sprite.canvas + buffer;
    sprite.buffer = buffer;
    sprite.file = file;
    sprite.sides = (uint8_t)sides;
    if (!set_side(side)) {
        free_buffers();
        sprite.file = NULL;
        return false;
    }
    return true;
}

/* The largest size of the animation's proportions inside the box, whole
 * factor or not, centred, on the box's bottom edge. The box holds the canvas
 * at 1x (open_file()), so w and h are at least its, which blit() relies on. */
static void fit(void) {
    const struct sprite_pack_anim *a = &sprite.anim;
    const int32_t box_w = lv_area_get_width(&sprite.box);
    const int32_t box_h = lv_area_get_height(&sprite.box);
    int32_t w, h;

    if ((int64_t)box_w * a->height <= (int64_t)box_h * a->width) {
        w = box_w;
        h = (int32_t)((int64_t)box_w * a->height / a->width);
    } else {
        h = box_h;
        w = (int32_t)((int64_t)box_h * a->width / a->height);
    }
    lv_obj_set_size(sprite.obj, w, h);
    lv_obj_set_pos(sprite.obj, sprite.box.x1 + (box_w - w) / 2, sprite.box.y1 + box_h - h);
}

void beacon_sprite_create(lv_obj_t *screen, const lv_area_t *box) {
    const size_t pool_before = IS_ENABLED(CONFIG_LOG) ? pool_allocated() : 0;

    sprite.box = *box;
    if (!open_file(sprite_embedded, sizeof(sprite_embedded), 0)) {
        LOG_ERR("sprite: pack rejected or no pool memory for it (%u byte file)",
                (unsigned int)sizeof(sprite_embedded));
        return;
    }
    const size_t pool_sprite = IS_ENABLED(CONFIG_LOG) ? pool_allocated() - pool_before : 0;

    /* A plain object that draws nothing of its own; blit() paints its area.
     * The first child, whatever the screen held before: blit()'s second
     * condition (header). */
    lv_obj_t *obj = lv_obj_create(screen);
    lv_obj_move_to_index(obj, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_outline_width(obj, 0, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    sprite.obj = obj;
    fit();
    lv_obj_add_event_cb(obj, blit, LV_EVENT_DRAW_MAIN, NULL);

    struct beacon_status status;
    beacon_status_get(&status);

    sprite.keys_seen = status.keystrokes;
    sprite.frame_at = lv_tick_get() * SUB_PER_MS;
    sprite.logged_at = lv_tick_get();
    sprite.shown = sprite.picture;
    sprite.timer = lv_timer_create(tick, TICK_MS, NULL);
    lv_display_t *disp = lv_obj_get_display(obj);
    lv_display_add_event_cb(disp, render_ready, LV_EVENT_RENDER_READY, NULL);
    if (IS_ENABLED(CONFIG_LOG)) {
        lv_display_add_event_cb(disp, count_render, LV_EVENT_RENDER_START, NULL);
        lv_display_add_event_cb(disp, count_render, LV_EVENT_RENDER_READY, NULL);
    }

    const int32_t h = lv_obj_get_style_height(obj, 0);

    LOG_INF("sprite as %dx%d (%d.%02dx) from a %u byte pack, %u bytes of the lvgl pool",
            (int)lv_obj_get_style_width(obj, 0), (int)h, (int)(h / sprite.anim.height),
            (int)(h * 100 / sprite.anim.height % 100), (unsigned int)sizeof(sprite_embedded),
            (unsigned int)pool_sprite);
}
