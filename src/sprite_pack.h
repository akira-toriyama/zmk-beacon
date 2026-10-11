/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * The sprite pack as tools/sprite_pack.py writes it (its docstring is the
 * byte layout, docs/sprite-pack.md the reasons). Plain C, no Zephyr: the same
 * file compiles on the host, where it was run against every packed GIF of the
 * user's collection (2026-10-06).
 *
 * A file holds one or two animations (front, back). An animation is a
 * palette, the distinct pictures it ever shows, and the order it shows them
 * in. A picture is a raw deflate stream of a band of canvas rows, packed 4 or
 * 8 bits a pixel (value 0 transparent, 1..colours the palette); picture 0 is
 * the whole canvas and the preset dictionary of every other picture, so any
 * picture decodes in one step from the file and that one buffer.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SPRITE_PACK_MAGIC "SPK1"
#define SPRITE_PACK_HEADER 32
#define SPRITE_PACK_NAME_LEN 15

struct sprite_pack_anim {
    const uint8_t *file;
    uint16_t width;
    uint16_t height;
    uint8_t bpp;
    uint8_t colours;
    uint16_t pictures;
    uint16_t frames;
    uint16_t stride;
    const uint8_t *palette; /* colours x RGB */
    const uint8_t *picture_table;
    const uint8_t *frame_table;
};

struct sprite_pack_picture {
    const uint8_t *stream;
    uint16_t length;
    uint8_t first_row;
    uint8_t rows;
};

/* The file's animation count and total length when its header is sound and
 * fits in avail bytes, else 0 animations. */
int sprite_pack_count(const uint8_t *file, size_t avail, uint32_t *total);

/* The file's name: at most SPRITE_PACK_NAME_LEN bytes, not NUL-terminated
 * before that; *len its length. */
const char *sprite_pack_name(const uint8_t *file, size_t *len);

/* True when every offset, table and stream of animation `index` lies inside
 * the file's total length and its values are consistent. */
bool sprite_pack_open(const uint8_t *file, int index, struct sprite_pack_anim *out);

void sprite_pack_picture(const struct sprite_pack_anim *a, uint16_t index,
                         struct sprite_pack_picture *out);
/* Frame `index`: its picture and delay in 10 ms. */
void sprite_pack_frame(const struct sprite_pack_anim *a, uint16_t index, uint16_t *picture,
                       uint16_t *delay);

/* Inflates one raw deflate stream into exactly out_len bytes. dict (dict_len
 * bytes) stands before the output for back references, NULL for none.
 * 0 on success, negative on malformed data or a stream of another length. */
int sprite_pack_inflate(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_len,
                        const uint8_t *dict, size_t dict_len);

/* Decodes picture `index` into canvas (stride x height bytes), the rows
 * outside its band cleared; dict is the decoded picture 0 (NULL while
 * decoding picture 0 itself). 0 on success. */
int sprite_pack_decode(const struct sprite_pack_anim *a, uint16_t index, uint8_t *canvas,
                       const uint8_t *dict);

/* CRC-32 (IEEE, as zlib) of the bytes the header's field covers: [16, total). */
uint32_t sprite_pack_crc(const uint8_t *file, uint32_t total);
