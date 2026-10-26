#!/usr/bin/env python3
"""The HP bar's katakana font, generated from the pkmn TTF.

  pkmn_font.py <pkmn_s.ttf> -o <dir>        writes font_pkmn.c and font_pkmn.h into dir
  pkmn_font.py preview <pkmn_s.ttf> <text>  prints text's glyphs as pixels, one row a line

pkmn (https://github.com/nue-of-k/pkmn, MIT, Copyright (c) 2009-2015 Nue)
redraws the letters of the first Game Boy games: every outline is a union of
squares on an 8-dot grid (unitsPerEm 1024, a dot 128 units), a glyph 8 dots
wide, 7 above the base line and 1 below, with a dakuten or handakuten in the
3 dots above that. This writes PKMN Strict (pkmn_s.ttf) at 2 px a dot, every
stroke BOLD_PX wider to the right and the bottom (the user found the plain
2 px strokes thin beside unscii 16, 2026-10-26), as an LVGL font
(lv_font_fmt_txt, 1 bit a pixel, uncompressed; the layout of lv_font_conv's
output, which LVGL's own unscii fonts share): a 16 px cell on a 23 px line,
the base line 3 px above its bottom. The glyphs are the whole of
sprite_pack.KATAKANA_RANGES, never a name's: the font is public and a name
names the subject. The TTF is not in the repository: clone pkmn and pass its
ttf/pkmn_s.ttf; the header records the file's SHA-256.

Standard library only (macOS /usr/bin/python3, 3.9). Exit 0, 2 on bad input
or a TTF this does not understand (curves, composites, coordinates off the
grid, a glyph of the ranges missing or without an outline: 29 of pkmn's
fullwidth forms are empty, so the ranges leave them out): the font is read
for this one typeface.
"""
import hashlib
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sprite_pack  # noqa: E402

SCALE = 2
# Pixels added to the right and the bottom of every stroke, after scaling.
BOLD_PX = 1
FONT = "beacon_font_pkmn"
PREFIX = "BEACON_FONT_PKMN"


class FontError(Exception):
    pass


class Glyph:
    """One glyph as dots: rows top to bottom, each a list of 0/1 from x_min;
    y_max is the top row's dot above the base line (exclusive), y_min the
    bottom row's (inclusive, negative below the base line)."""

    def __init__(self, rows, x_min, y_min, adv):
        self.rows = rows
        self.x_min = x_min
        self.y_min = y_min
        self.adv = adv

    @property
    def width(self):
        return len(self.rows[0]) if self.rows else 0

    @property
    def height(self):
        return len(self.rows)

    @property
    def y_max(self):
        return self.y_min + self.height


class TrueType:
    def __init__(self, data):
        self.data = data
        n = struct.unpack_from(">H", data, 4)[0]
        self.tables = {}
        for i in range(n):
            tag, _, off, length = struct.unpack_from(">4sIII", data, 12 + 16 * i)
            self.tables[tag.decode("latin-1")] = (off, length)
        for tag in ("head", "maxp", "hhea", "hmtx", "cmap", "loca", "glyf"):
            if tag not in self.tables:
                raise FontError("no %s table" % tag)
        head = self.tables["head"][0]
        self.units_per_em = struct.unpack_from(">H", data, head + 18)[0]
        self.long_loca = struct.unpack_from(">h", data, head + 50)[0] != 0
        self.num_glyphs = struct.unpack_from(">H", data, self.tables["maxp"][0] + 4)[0]
        self.num_hmetrics = struct.unpack_from(">H", data, self.tables["hhea"][0] + 34)[0]
        self.cmap = self._cmap()

    def _cmap(self):
        base = self.tables["cmap"][0]
        n = struct.unpack_from(">H", self.data, base + 2)[0]
        subtables = {}
        for i in range(n):
            pid, eid, off = struct.unpack_from(">HHI", self.data, base + 4 + 8 * i)
            subtables[(pid, eid)] = base + off
        for key in ((3, 10), (0, 4), (3, 1), (0, 3)):
            if key in subtables:
                off = subtables[key]
                fmt = struct.unpack_from(">H", self.data, off)[0]
                if fmt == 12:
                    return self._cmap12(off)
                if fmt == 4:
                    return self._cmap4(off)
        raise FontError("no Unicode cmap of format 4 or 12")

    def _cmap12(self, off):
        groups = struct.unpack_from(">I", self.data, off + 12)[0]
        mapping = {}
        for i in range(groups):
            start, end, gid = struct.unpack_from(">III", self.data, off + 16 + 12 * i)
            for c in range(start, end + 1):
                mapping[c] = gid + c - start
        return mapping

    def _cmap4(self, off):
        seg_x2 = struct.unpack_from(">H", self.data, off + 6)[0]
        seg = seg_x2 // 2
        ends = struct.unpack_from(">%dH" % seg, self.data, off + 14)
        starts = struct.unpack_from(">%dH" % seg, self.data, off + 16 + seg_x2)
        deltas = struct.unpack_from(">%dh" % seg, self.data, off + 16 + 2 * seg_x2)
        range_pos = off + 16 + 3 * seg_x2
        range_offs = struct.unpack_from(">%dH" % seg, self.data, range_pos)
        mapping = {}
        for s in range(seg):
            for c in range(starts[s], min(ends[s], 0xFFFE) + 1):
                if range_offs[s] == 0:
                    gid = (c + deltas[s]) & 0xFFFF
                else:
                    at = range_pos + 2 * s + range_offs[s] + 2 * (c - starts[s])
                    gid = struct.unpack_from(">H", self.data, at)[0]
                    if gid:
                        gid = (gid + deltas[s]) & 0xFFFF
                if gid:
                    mapping[c] = gid
        return mapping

    def advance(self, gid):
        i = min(gid, self.num_hmetrics - 1)
        return struct.unpack_from(">H", self.data, self.tables["hmtx"][0] + 4 * i)[0]

    def contours(self, gid):
        """The glyph's closed contours as lists of (x, y) in font units; []
        for an empty glyph. FontError on a composite or a curve."""
        loca = self.tables["loca"][0]
        if self.long_loca:
            start, end = struct.unpack_from(">II", self.data, loca + 4 * gid)
        else:
            start, end = (2 * v for v in struct.unpack_from(">HH", self.data, loca + 2 * gid))
        if start == end:
            return []
        at = self.tables["glyf"][0] + start
        n = struct.unpack_from(">h", self.data, at)[0]
        if n < 0:
            raise FontError("glyph %d is a composite" % gid)
        end_pts = struct.unpack_from(">%dH" % n, self.data, at + 10)
        at += 10 + 2 * n
        instructions = struct.unpack_from(">H", self.data, at)[0]
        at += 2 + instructions
        points = end_pts[-1] + 1 if n else 0
        flags = []
        while len(flags) < points:
            f = self.data[at]
            at += 1
            repeat = 1
            if f & 0x08:
                repeat += self.data[at]
                at += 1
            flags.extend([f] * repeat)
        flags = flags[:points]
        xs, at = self._coords(flags, at, 0x02, 0x10)
        ys, at = self._coords(flags, at, 0x04, 0x20)
        contours = []
        first = 0
        for last in end_pts:
            contour = []
            for i in range(first, last + 1):
                if not flags[i] & 0x01:
                    raise FontError("glyph %d has a curve" % gid)
                contour.append((xs[i], ys[i]))
            contours.append(contour)
            first = last + 1
        return contours

    def _coords(self, flags, at, short_bit, same_bit):
        out = []
        v = 0
        for f in flags:
            if f & short_bit:
                d = self.data[at]
                at += 1
                v += d if f & same_bit else -d
            elif not f & same_bit:
                v += struct.unpack_from(">h", self.data, at)[0]
                at += 2
            out.append(v)
        return out, at


def winding(contours, x2, y2):
    """The winding number of the point (x2/2, y2/2) in the contours."""
    wn = 0
    for contour in contours:
        for i, (x0, y0) in enumerate(contour):
            x1, y1 = contour[(i + 1) % len(contour)]
            ax, ay, bx, by = 2 * x0, 2 * y0, 2 * x1, 2 * y1
            if ay == by:
                continue
            if min(ay, by) <= y2 < max(ay, by):
                side = (bx - ax) * (y2 - ay) - (x2 - ax) * (by - ay)
                if by > ay and side > 0:
                    wn += 1
                elif by < ay and side < 0:
                    wn -= 1
    return wn


def rasterize(ttf, gid, dot):
    """The glyph as a Glyph of dots, trimmed to its ink; FontError without any."""
    adv = ttf.advance(gid)
    if adv % dot:
        raise FontError("glyph %d: advance %d is not whole dots" % (gid, adv))
    contours = ttf.contours(gid)
    if not contours:
        raise FontError("glyph %d has no outline" % gid)
    for contour in contours:
        for x, y in contour:
            if x % dot or y % dot:
                raise FontError("glyph %d: a point off the dot grid" % gid)
    xs = [x // dot for c in contours for x, _ in c]
    ys = [y // dot for c in contours for _, y in c]
    x_min, x_max, y_min, y_max = min(xs), max(xs), min(ys), max(ys)
    rows = []
    for y in range(y_max - 1, y_min - 1, -1):
        row = []
        for x in range(x_min, x_max):
            row.append(1 if winding(contours, (2 * x + 1) * dot, (2 * y + 1) * dot) else 0)
        rows.append(row)
    while rows and not any(rows[0]):
        rows.pop(0)
        y_max -= 1
    while rows and not any(rows[-1]):
        rows.pop()
        y_min += 1
    if not rows:
        raise FontError("glyph %d has no ink" % gid)
    while not any(r[0] for r in rows):
        rows = [r[1:] for r in rows]
        x_min += 1
    while not any(r[-1] for r in rows):
        rows = [r[:-1] for r in rows]
    return Glyph(rows, x_min, y_min, adv // dot)


def codepoints():
    out = []
    for lo, hi in sprite_pack.KATAKANA_RANGES:
        out.extend(range(lo, hi + 1))
    return sorted(out)


def read_glyphs(ttf):
    """[(codepoint, Glyph)] over codepoints(), sorted; FontError on one the TTF lacks."""
    dot = ttf.units_per_em // 8
    if dot * 8 != ttf.units_per_em:
        raise FontError("unitsPerEm %d is not 8 dots" % ttf.units_per_em)
    glyphs = []
    for c in codepoints():
        gid = ttf.cmap.get(c)
        if not gid:
            raise FontError("U+%04X is not in the font" % c)
        try:
            glyphs.append((c, rasterize(ttf, gid, dot)))
        except FontError as e:
            raise FontError("U+%04X: %s" % (c, e))
    return glyphs


def pixels(glyph):
    """The glyph's pixel rows at SCALE, each stroke BOLD_PX wider to the right
    and the bottom: (width, height, rows of 0/1)."""
    width = glyph.width * SCALE + BOLD_PX
    height = glyph.height * SCALE + BOLD_PX
    rows = []
    for row in glyph.rows:
        line = [bit for bit in row for _ in range(SCALE)] + [0] * BOLD_PX
        for x in range(width - 1, -1, -1):
            if any(line[x - d] for d in range(1, BOLD_PX + 1) if x - d >= 0):
                line[x] = 1
        for _ in range(SCALE):
            rows.append(line)
    rows += [[0] * width for _ in range(BOLD_PX)]
    for y in range(height - 1, -1, -1):
        rows[y] = [1 if rows[y][x] or any(rows[y - d][x] for d in range(1, BOLD_PX + 1) if y - d >= 0) else 0
                   for x in range(width)]
    return width, height, rows


def bitmap(glyph):
    """The glyph's pixels, rows top to bottom, bits packed MSB first without
    row padding, as lv_font_get_bitmap_fmt_txt reads 1 bpp."""
    _, _, rows = pixels(glyph)
    bits = [bit for row in rows for bit in row]
    out = bytearray()
    for i in range(0, len(bits), 8):
        byte = 0
        for bit in bits[i:i + 8]:
            byte = (byte << 1) | bit
        byte <<= 8 - len(bits[i:i + 8])
        out.append(byte)
    return bytes(out)


def metrics(glyphs):
    """(line height, base line, rows above the cell) in pixels: the line holds
    every glyph, bold included, the cell is the 8 dots the base line splits 7
    and 1."""
    top = max(g.y_max for _, g in glyphs) * SCALE
    bottom = min(g.y_min for _, g in glyphs) * SCALE - BOLD_PX
    return top - bottom, -bottom, top - 7 * SCALE


def c_bytes(data, indent="    "):
    lines = []
    for i in range(0, len(data), 8):
        lines.append(indent + ", ".join("0x%02x" % b for b in data[i:i + 8]) + ",")
    return "\n".join(lines)


NOTICE = """\
 * The glyphs are PKMN Strict from pkmn (https://github.com/nue-of-k/pkmn):
 *
 * MIT License
 *
 * Copyright (c) 2009-2015 Nue
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
"""


def generated_by(sha):
    return (" * Generated by tools/pkmn_font.py from pkmn_s.ttf (SHA-256 %s)\n"
            " * at %d px a dot, strokes %d px bolder. Do not edit: run the tool again.\n"
            % (sha, SCALE, BOLD_PX))


def header_text(glyphs, sha):
    line_h, base, above = metrics(glyphs)
    return """\
/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
%s *
 * The HP bar's katakana font (hp_bar.c): the glyphs of
 * sprite_pack.KATAKANA_RANGES, each in a %d px cell (8 dots: 7 above the
 * base line, 1 below, plus the bold's pixel under it) on a %d px line whose
 * top %d px hold a dakuten or handakuten. Every glyph advances one cell.
 */

#pragma once

#include <lvgl.h>

#define %s_LINE_H %d
#define %s_BASE_LINE %d
#define %s_ABOVE_CELL %d
#define %s_CELL %d

LV_FONT_DECLARE(%s);
""" % (generated_by(sha), 8 * SCALE, line_h, above, PREFIX, line_h, PREFIX, base, PREFIX, above,
       PREFIX, 8 * SCALE, FONT)


def source_text(glyphs, sha):
    line_h, base, _ = metrics(glyphs)
    bitmaps = []
    dscs = ["    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,"]
    index = 0
    for c, g in glyphs:
        data = bitmap(g)
        width, height, _ = pixels(g)
        bitmaps.append("    /* U+%04X */\n%s" % (c, c_bytes(data)))
        dscs.append("    {.bitmap_index = %d, .adv_w = %d, .box_w = %d, .box_h = %d, .ofs_x = %d, .ofs_y = %d}," % (
            index, g.adv * SCALE * 16, width, height, g.x_min * SCALE, g.y_min * SCALE - BOLD_PX))
        index += len(data)
    cmaps = []
    start = 0
    while start < len(glyphs):
        end = start
        while end + 1 < len(glyphs) and glyphs[end + 1][0] == glyphs[end][0] + 1:
            end += 1
        cmaps.append("""    {
        .range_start = %d, .range_length = %d, .glyph_id_start = %d,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    },""" % (glyphs[start][0], end - start + 1, start + 1))
        start = end + 1
    return """\
/*
 * Copyright (c) 2026 akira-toriyama
 *
 * SPDX-License-Identifier: MIT
 *
%s *
%s */

#include "font_pkmn.h"

static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
%s
};

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
%s
};

static const lv_font_fmt_txt_cmap_t cmaps[] = {
%s
};

static const lv_font_fmt_txt_dsc_t font_dsc = {
    .glyph_bitmap = glyph_bitmap,
    .glyph_dsc = glyph_dsc,
    .cmaps = cmaps,
    .kern_dsc = NULL,
    .kern_scale = 0,
    .cmap_num = %d,
    .bpp = 1,
    .kern_classes = 0,
    .bitmap_format = 0,
};

const lv_font_t %s = {
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,
    .line_height = %d,
    .base_line = %d,
    .subpx = LV_FONT_SUBPX_NONE,
    .underline_position = 0,
    .underline_thickness = 0,
    .dsc = &font_dsc,
};
""" % (generated_by(sha), NOTICE, "\n\n".join(bitmaps), "\n".join(dscs), "\n".join(cmaps), len(cmaps),
       FONT, line_h, base)


def preview(ttf, text):
    """text as the device draws it: a pixel a character, the line's rows top
    to bottom, each glyph in its cell."""
    dot = ttf.units_per_em // 8
    line_h, base, _ = metrics(read_glyphs(ttf))
    glyphs = []
    for ch in text:
        gid = ttf.cmap.get(ord(ch))
        if not gid:
            raise FontError("U+%04X is not in the font" % ord(ch))
        glyphs.append(rasterize(ttf, gid, dot))
    lines = []
    for y in range(line_h):
        line = ""
        for g in glyphs:
            cell = ["."] * (g.adv * SCALE)
            width, height, rows = pixels(g)
            top = (line_h - base) - height - (g.y_min * SCALE - BOLD_PX)
            if top <= y < top + height:
                for i, bit in enumerate(rows[y - top]):
                    if bit and g.x_min * SCALE + i < len(cell):
                        cell[g.x_min * SCALE + i] = "#"
            line += "".join(cell) + " "
        lines.append(line.rstrip())
    return "\n".join(lines)


def main(argv):
    try:
        if len(argv) == 3 and argv[0] == "preview":
            with open(argv[1], "rb") as f:
                ttf = TrueType(f.read())
            print(preview(ttf, argv[2]))
            return 0
        if len(argv) == 3 and argv[1] == "-o":
            with open(argv[0], "rb") as f:
                data = f.read()
            ttf = TrueType(data)
            glyphs = read_glyphs(ttf)
            sha = hashlib.sha256(data).hexdigest()
            with open(os.path.join(argv[2], "font_pkmn.h"), "w", encoding="utf-8") as f:
                f.write(header_text(glyphs, sha))
            with open(os.path.join(argv[2], "font_pkmn.c"), "w", encoding="utf-8") as f:
                f.write(source_text(glyphs, sha))
            line_h, base, above = metrics(glyphs)
            total = sum(len(bitmap(g)) for _, g in glyphs)
            print("%d glyphs, %d bitmap bytes, line %d px, base line %d, %d above the cell" % (
                len(glyphs), total, line_h, base, above))
            return 0
    except (FontError, OSError, struct.error) as e:
        print("pkmn_font: %s" % e, file=sys.stderr)
        return 2
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
