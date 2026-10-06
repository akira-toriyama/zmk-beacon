/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
 * sprite_pack.h. The inflater follows RFC 1951 the way zlib's contrib/puff
 * does (canonical Huffman decoding a bit at a time, no lookup tables): small,
 * and every write is bounds-checked, so erased or foreign flash decodes to an
 * error, never past the canvas. A 153x94 picture inflates in 6-9 ms on the
 * nRF52840 (hardware 2026-10-06). Its tables live in one static block: the
 * display work queue is the only caller.
 */

#include <stdbool.h>
#include <string.h>

#include "sprite_pack.h"

#define MAXBITS 15
#define MAXLCODES 286
#define MAXDCODES 30
#define FIXLCODES 288

struct huffman {
    uint16_t count[MAXBITS + 1];
    uint16_t symbol[FIXLCODES];
};

struct inflater {
    const uint8_t *in;
    size_t in_len;
    size_t in_pos;
    uint32_t bitbuf;
    int bitcnt;
    uint8_t *out;
    size_t out_len;
    size_t out_pos;
    const uint8_t *dict;
    size_t dict_len;
};

static struct {
    struct huffman lencode;
    struct huffman distcode;
    uint16_t lengths[MAXLCODES + MAXDCODES];
    bool fixed_built;
    struct huffman fixed_len;
    struct huffman fixed_dist;
} st;

static const uint16_t length_base[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                         31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const uint8_t length_extra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                         2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const uint16_t dist_base[30] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,
                                       33,  49,  65,  97,  129, 193,  257,  385,  513,  769,
                                       1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const uint8_t dist_extra[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                       6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
static const uint8_t clen_order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

static uint32_t le16(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8; }
static uint32_t le32(const uint8_t *p) { return le16(p) | le16(p + 2) << 16; }

static int bits(struct inflater *s, int need) {
    uint32_t val = s->bitbuf;

    while (s->bitcnt < need) {
        if (s->in_pos == s->in_len) {
            return -1;
        }
        val |= (uint32_t)s->in[s->in_pos++] << s->bitcnt;
        s->bitcnt += 8;
    }
    s->bitbuf = val >> need;
    s->bitcnt -= need;
    return (int)(val & ((1u << need) - 1));
}

static int decode(struct inflater *s, const struct huffman *h) {
    int code = 0, first = 0, index = 0;

    for (int len = 1; len <= MAXBITS; len++) {
        int bit = bits(s, 1);
        if (bit < 0) {
            return bit;
        }
        code |= bit;
        int count = h->count[len];
        if (code - count < first) {
            return h->symbol[index + (code - first)];
        }
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -10;
}

/* 0 complete, > 0 incomplete (fine for a single code), < 0 over-subscribed. */
static int construct(struct huffman *h, const uint16_t *length, int n) {
    uint16_t offs[MAXBITS + 1];

    memset(h->count, 0, sizeof(h->count));
    for (int sym = 0; sym < n; sym++) {
        h->count[length[sym]]++;
    }
    if (h->count[0] == n) {
        return 0;
    }
    int left = 1;
    for (int len = 1; len <= MAXBITS; len++) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0) {
            return left;
        }
    }
    offs[1] = 0;
    for (int len = 1; len < MAXBITS; len++) {
        offs[len + 1] = offs[len] + h->count[len];
    }
    for (int sym = 0; sym < n; sym++) {
        if (length[sym] != 0) {
            h->symbol[offs[length[sym]]++] = sym;
        }
    }
    return left;
}

static int codes(struct inflater *s, const struct huffman *lencode, const struct huffman *distcode) {
    for (;;) {
        int sym = decode(s, lencode);
        if (sym < 0) {
            return sym;
        }
        if (sym < 256) {
            if (s->out_pos == s->out_len) {
                return -11;
            }
            s->out[s->out_pos++] = (uint8_t)sym;
            continue;
        }
        if (sym == 256) {
            return 0;
        }
        sym -= 257;
        if (sym >= 29) {
            return -12;
        }
        int extra = bits(s, length_extra[sym]);
        if (extra < 0) {
            return extra;
        }
        size_t len = length_base[sym] + extra;
        sym = decode(s, distcode);
        if (sym < 0) {
            return sym;
        }
        if (sym >= 30) {
            return -13;
        }
        extra = bits(s, dist_extra[sym]);
        if (extra < 0) {
            return extra;
        }
        size_t dist = dist_base[sym] + extra;
        if (dist > s->out_pos + s->dict_len || len > s->out_len - s->out_pos) {
            return -14;
        }
        while (len-- > 0) {
            uint8_t b = dist > s->out_pos ? s->dict[s->dict_len - (dist - s->out_pos)]
                                          : s->out[s->out_pos - dist];
            s->out[s->out_pos++] = b;
        }
    }
}

static int stored(struct inflater *s) {
    s->bitbuf = 0;
    s->bitcnt = 0;
    if (s->in_pos + 4 > s->in_len) {
        return -2;
    }
    size_t len = le16(s->in + s->in_pos);
    size_t nlen = le16(s->in + s->in_pos + 2);
    s->in_pos += 4;
    if (len != (~nlen & 0xffff)) {
        return -3;
    }
    if (s->in_pos + len > s->in_len || s->out_pos + len > s->out_len) {
        return -4;
    }
    memcpy(s->out + s->out_pos, s->in + s->in_pos, len);
    s->in_pos += len;
    s->out_pos += len;
    return 0;
}

static int fixed(struct inflater *s) {
    if (!st.fixed_built) {
        uint16_t lengths[FIXLCODES];
        int sym = 0;

        for (; sym < 144; sym++) lengths[sym] = 8;
        for (; sym < 256; sym++) lengths[sym] = 9;
        for (; sym < 280; sym++) lengths[sym] = 7;
        for (; sym < FIXLCODES; sym++) lengths[sym] = 8;
        construct(&st.fixed_len, lengths, FIXLCODES);
        for (sym = 0; sym < MAXDCODES; sym++) lengths[sym] = 5;
        construct(&st.fixed_dist, lengths, MAXDCODES);
        st.fixed_built = true;
    }
    return codes(s, &st.fixed_len, &st.fixed_dist);
}

static int dynamic(struct inflater *s) {
    int nlen, ndist, ncode;
    uint16_t *lengths = st.lengths;

    if ((nlen = bits(s, 5)) < 0 || (ndist = bits(s, 5)) < 0 || (ncode = bits(s, 4)) < 0) {
        return -1;
    }
    nlen += 257;
    ndist += 1;
    ncode += 4;
    if (nlen > MAXLCODES || ndist > MAXDCODES) {
        return -5;
    }
    for (int i = 0; i < 19; i++) {
        lengths[clen_order[i]] = 0;
    }
    for (int i = 0; i < ncode; i++) {
        int v = bits(s, 3);
        if (v < 0) {
            return v;
        }
        lengths[clen_order[i]] = v;
    }
    if (construct(&st.lencode, lengths, 19) != 0) {
        return -6;
    }
    int index = 0;
    while (index < nlen + ndist) {
        int sym = decode(s, &st.lencode);
        if (sym < 0) {
            return sym;
        }
        if (sym < 16) {
            lengths[index++] = sym;
            continue;
        }
        int len = 0, rep;
        if (sym == 16) {
            if (index == 0) {
                return -7;
            }
            len = lengths[index - 1];
            rep = bits(s, 2);
            if (rep < 0) {
                return rep;
            }
            rep += 3;
        } else if (sym == 17) {
            rep = bits(s, 3);
            if (rep < 0) {
                return rep;
            }
            rep += 3;
        } else {
            rep = bits(s, 7);
            if (rep < 0) {
                return rep;
            }
            rep += 11;
        }
        if (index + rep > nlen + ndist) {
            return -8;
        }
        while (rep-- > 0) {
            lengths[index++] = len;
        }
    }
    if (lengths[256] == 0) {
        return -9;
    }
    int err = construct(&st.lencode, lengths, nlen);
    if (err < 0 || (err > 0 && nlen - st.lencode.count[0] != 1)) {
        return -6;
    }
    err = construct(&st.distcode, lengths + nlen, ndist);
    if (err < 0 || (err > 0 && ndist - st.distcode.count[0] != 1)) {
        return -6;
    }
    return codes(s, &st.lencode, &st.distcode);
}

int sprite_pack_inflate(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_len,
                        const uint8_t *dict, size_t dict_len) {
    struct inflater s = {
        .in = in, .in_len = in_len, .out = out, .out_len = out_len,
        .dict = dict, .dict_len = dict == NULL ? 0 : dict_len,
    };
    int last, type, err;

    do {
        if ((last = bits(&s, 1)) < 0 || (type = bits(&s, 2)) < 0) {
            return -1;
        }
        err = type == 0 ? stored(&s) : type == 1 ? fixed(&s) : type == 2 ? dynamic(&s) : -15;
        if (err != 0) {
            return err;
        }
    } while (!last);
    return s.out_pos == out_len ? 0 : -16;
}

int sprite_pack_count(const uint8_t *file, size_t avail, uint32_t *total) {
    if (avail < SPRITE_PACK_HEADER || memcmp(file, SPRITE_PACK_MAGIC, 4) != 0) {
        return 0;
    }
    uint32_t n = le16(file + 4);
    uint32_t len = le32(file + 8);
    if (n == 0 || len > avail || len < SPRITE_PACK_HEADER + 4 * n) {
        return 0;
    }
    *total = len;
    return (int)n;
}

const char *sprite_pack_name(const uint8_t *file, size_t *len) {
    const char *name = (const char *)file + 16;
    size_t n = 0;

    while (n < SPRITE_PACK_NAME_LEN && name[n] != '\0') {
        n++;
    }
    *len = n;
    return name;
}

bool sprite_pack_open(const uint8_t *file, int index, struct sprite_pack_anim *a) {
    uint32_t total;
    int n = sprite_pack_count(file, SIZE_MAX, &total);

    if (index < 0 || index >= n) {
        return false;
    }
    uint32_t base = le32(file + SPRITE_PACK_HEADER + 4 * index);
    if (base % 4 != 0 || base + 16 > total) {
        return false;
    }
    const uint8_t *h = file + base;
    a->file = file;
    a->width = le16(h);
    a->height = le16(h + 2);
    a->bpp = h[4];
    a->colours = h[5];
    a->pictures = le16(h + 6);
    a->frames = le16(h + 8);
    a->stride = le16(h + 10);
    if (a->width == 0 || a->height == 0 || a->height > 255 || a->pictures == 0 || a->frames == 0 ||
        a->colours == 0 || !((a->bpp == 4 && a->colours <= 15) || (a->bpp == 8 && a->colours <= 255)) ||
        a->stride != (a->bpp == 4 ? (a->width + 1) / 2 : a->width)) {
        return false;
    }
    uint32_t pos = base + 16;
    a->palette = file + pos;
    pos += 3u * a->colours;
    pos += (4 - pos % 4) % 4;
    a->picture_table = file + pos;
    pos += 8u * a->pictures;
    a->frame_table = file + pos;
    pos += 4u * a->frames;
    if (pos > total) {
        return false;
    }
    for (uint16_t i = 0; i < a->pictures; i++) {
        struct sprite_pack_picture p;
        sprite_pack_picture(a, i, &p);
        uint32_t off = (uint32_t)(p.stream - file);
        if (off < pos || off + p.length > total || p.first_row + p.rows > a->height ||
            (i == 0 && (p.first_row != 0 || p.rows != a->height))) {
            return false;
        }
    }
    for (uint16_t i = 0; i < a->frames; i++) {
        if (le16(a->frame_table + 4 * i) >= a->pictures) {
            return false;
        }
    }
    return true;
}

void sprite_pack_picture(const struct sprite_pack_anim *a, uint16_t index,
                         struct sprite_pack_picture *out) {
    const uint8_t *e = a->picture_table + 8 * index;

    out->stream = a->file + le32(e);
    out->length = le16(e + 4);
    out->first_row = e[6];
    out->rows = e[7];
}

void sprite_pack_frame(const struct sprite_pack_anim *a, uint16_t index, uint16_t *picture,
                       uint16_t *delay) {
    const uint8_t *e = a->frame_table + 4 * index;

    *picture = le16(e);
    *delay = le16(e + 2);
}

int sprite_pack_decode(const struct sprite_pack_anim *a, uint16_t index, uint8_t *canvas,
                       const uint8_t *dict) {
    struct sprite_pack_picture p;
    const size_t stride = a->stride;

    if (index >= a->pictures) {
        return -20;
    }
    sprite_pack_picture(a, index, &p);
    memset(canvas, 0, p.first_row * stride);
    memset(canvas + (p.first_row + p.rows) * stride, 0, (a->height - p.first_row - p.rows) * stride);
    return sprite_pack_inflate(p.stream, p.length, canvas + p.first_row * stride, p.rows * stride,
                               dict, stride * a->height);
}

uint32_t sprite_pack_crc(const uint8_t *file, uint32_t total) {
    uint32_t crc = 0xffffffffu;

    for (uint32_t i = 16; i < total; i++) {
        crc ^= file[i];
        for (int k = 0; k < 8; k++) {
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
        }
    }
    return ~crc;
}
