"""Tests of gifpics.py and sprite_pack.py on GIFs written here (no personal
file enters the repository): python3 -m unittest discover tools."""
import os
import struct
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gifpics  # noqa: E402
import sprite_pack  # noqa: E402


def lzw_encode(min_size, pixels):
    """A GIF image data stream: code size byte, sub-blocks, terminator."""
    clear = 1 << min_size
    eoi = clear + 1
    next_code = clear + 2
    code_size = min_size + 1
    bits = []  # (code, width)
    table = {}
    bits.append((clear, code_size))
    if pixels:
        prefix = pixels[0]
        for px in pixels[1:]:
            key = (prefix << 8) | px
            code = table.get(key)
            if code is not None:
                prefix = code
                continue
            bits.append((prefix, code_size))
            if next_code < 4096:
                table[key] = next_code
                if next_code == (1 << code_size):
                    code_size += 1
                next_code += 1
            else:
                bits.append((clear, code_size))
                table = {}
                next_code = clear + 2
                code_size = min_size + 1
            prefix = px
        bits.append((prefix, code_size))
    bits.append((eoi, code_size))
    out = bytearray()
    acc = 0
    n = 0
    for code, width in bits:
        acc |= code << n
        n += width
        while n >= 8:
            out.append(acc & 0xFF)
            acc >>= 8
            n -= 8
    if n:
        out.append(acc & 0xFF)
    blocks = b"".join(bytes([len(out[i:i + 255])]) + bytes(out[i:i + 255]) for i in range(0, len(out), 255))
    return bytes([min_size]) + blocks + b"\0"


def gif(width, height, gct, frames, version=b"GIF89a", trailer=True):
    """A GIF: gct = [(r, g, b)...] (a power of two long), frames = dicts with
    rect, pixels (indices, row-major), and optional delay, disposal,
    transparent, interlaced, lct, control (False to omit the GCE)."""
    out = bytearray(version)
    out += struct.pack("<HHBBB", width, height, 0x80 | (len(gct).bit_length() - 2), 0, 0)
    out += b"".join(bytes(c) for c in gct)
    for fr in frames:
        if fr.get("control", True):
            flags = fr.get("disposal", 0) << 2 | (1 if fr.get("transparent") is not None else 0)
            out += b"\x21\xF9\x04" + bytes([flags]) + struct.pack("<H", fr.get("delay", 0))
            out += bytes([fr.get("transparent") or 0, 0])
        x, y, w, h = fr["rect"]
        lct = fr.get("lct")
        iflags = (0x80 | (len(lct).bit_length() - 2) if lct else 0) | (0x40 if fr.get("interlaced") else 0)
        out += b"\x2C" + struct.pack("<HHHHB", x, y, w, h, iflags)
        if lct:
            out += b"".join(bytes(c) for c in lct)
        px = fr["pixels"]
        if fr.get("interlaced"):
            rows = [px[r * w:(r + 1) * w] for r in range(h)]
            order = [r for start, step in ((0, 8), (4, 8), (2, 4), (1, 2)) for r in range(start, h, step)]
            px = b"".join(rows[r] for r in order)
        ncol = len(lct or gct)
        out += lzw_encode(max(2, (ncol - 1).bit_length()), px)
    if trailer:
        out += b"\x3B"
    return bytes(out)


PAL4 = [(0, 0, 0), (255, 0, 0), (0, 255, 0), (0, 0, 255)]


def square(w, h, value):
    return bytes([value]) * (w * h)


class GifPicsTest(unittest.TestCase):
    def test_one_frame_no_transparency(self):
        a = gifpics.animation(gif(3, 2, PAL4, [{"rect": (0, 0, 3, 2), "pixels": bytes([1, 2, 3, 3, 2, 1])}]))
        self.assertEqual((a.width, a.height), (3, 2))
        self.assertEqual(a.palette, [(255, 0, 0), (0, 255, 0), (0, 0, 255)])
        self.assertEqual(a.pictures, [bytes([1, 2, 3, 3, 2, 1])])
        self.assertEqual(a.order, [(0, 0)])

    def test_transparency_and_disposal(self):
        # Frame 1 paints the whole canvas red; frame 2 a transparent-holed
        # green square (disposal 2) over the middle; frame 3 a blue pixel
        # (disposal 3); frame 4 paints nothing new.
        frames = [
            {"rect": (0, 0, 4, 4), "pixels": square(4, 4, 1), "disposal": 1, "delay": 5},
            {"rect": (1, 1, 2, 2), "pixels": bytes([2, 0, 0, 2]), "transparent": 0, "disposal": 2, "delay": 1},
            {"rect": (0, 0, 1, 1), "pixels": bytes([3]), "disposal": 3, "delay": 0},
            {"rect": (3, 3, 1, 1), "pixels": bytes([0]), "transparent": 0, "delay": 20},
        ]
        a = gifpics.animation(gif(4, 4, PAL4, frames))
        red, green, blue = 1, 2, 3
        p1 = square(4, 4, red)
        p2 = bytearray(p1)
        p2[1 * 4 + 1] = green
        p2[2 * 4 + 2] = green
        p3 = bytearray(p1)
        p3[1 * 4 + 1] = 0
        p3[1 * 4 + 2] = 0
        p3[2 * 4 + 1] = 0
        p3[2 * 4 + 2] = 0
        p3[0] = blue
        p4 = bytearray(p3)
        p4[0] = red
        self.assertEqual(a.pictures, [p1, bytes(p2), bytes(p3), bytes(p4)])
        self.assertEqual(a.order, [(0, 5), (1, 1), (2, 0), (3, 20)])

    def test_same_picture_once(self):
        frames = [{"rect": (0, 0, 2, 1), "pixels": bytes([1, 1]), "delay": 3},
                  {"rect": (0, 0, 2, 1), "pixels": bytes([2, 2]), "delay": 3},
                  {"rect": (0, 0, 2, 1), "pixels": bytes([1, 1]), "delay": 7}]
        a = gifpics.animation(gif(2, 1, PAL4, frames))
        self.assertEqual(len(a.pictures), 2)
        self.assertEqual(a.order, [(0, 3), (1, 3), (0, 7)])

    def test_interlaced_equals_plain(self):
        px = bytes((r * 7 + c) % 3 + 1 for r in range(9) for c in range(5))
        plain = gifpics.animation(gif(5, 9, PAL4, [{"rect": (0, 0, 5, 9), "pixels": px}]))
        laced = gifpics.animation(gif(5, 9, PAL4, [{"rect": (0, 0, 5, 9), "pixels": px, "interlaced": True}]))
        self.assertEqual(plain.pictures, laced.pictures)

    def test_local_colour_table(self):
        lct = [(9, 9, 9), (8, 8, 8)]
        frames = [{"rect": (0, 0, 1, 1), "pixels": bytes([1])},
                  {"rect": (0, 0, 1, 1), "pixels": bytes([1]), "lct": lct}]
        a = gifpics.animation(gif(1, 1, PAL4, frames))
        self.assertEqual(a.palette, [(255, 0, 0), (8, 8, 8)])
        self.assertEqual(a.pictures, [bytes([1]), bytes([2])])

    def test_frame_without_control_and_clipping(self):
        frames = [{"rect": (1, 1, 3, 3), "pixels": square(3, 3, 2), "control": False}]
        a = gifpics.animation(gif(2, 2, PAL4, frames))
        self.assertEqual(a.palette, [(0, 255, 0)])
        self.assertEqual(a.pictures, [bytes([0, 0, 0, 1])])
        self.assertEqual(a.order, [(0, 0)])

    def test_short_and_long_pixel_streams(self):
        a = gifpics.animation(gif(2, 2, PAL4, [{"rect": (0, 0, 2, 2), "pixels": bytes([1, 2, 3, 1, 2, 2])}]))
        self.assertEqual(a.pictures, [bytes([1, 2, 3, 1])])
        a = gifpics.animation(gif(2, 2, PAL4, [{"rect": (0, 0, 2, 2), "pixels": bytes([3, 1]), "transparent": 0}]))
        # Values follow the frame's colour indices in ascending order.
        self.assertEqual(a.palette, [(255, 0, 0), (0, 0, 255)])
        self.assertEqual(a.pictures, [bytes([2, 1, 0, 0])])

    def test_cut_file_plays_the_complete_frames(self):
        data = gif(2, 1, PAL4, [{"rect": (0, 0, 2, 1), "pixels": bytes([1, 2])},
                                {"rect": (0, 0, 2, 1), "pixels": bytes([2, 1])}], trailer=False)
        a = gifpics.animation(data[:-3])
        self.assertEqual(len(a.order), 1)
        with self.assertRaises(gifpics.GifError):
            gifpics.animation(data[:20])

    def test_errors(self):
        with self.assertRaises(gifpics.GifError):
            gifpics.animation(b"PNG\r\n")
        with self.assertRaises(gifpics.GifError):
            gifpics.animation(gif(2, 1, PAL4, []))
        many = [(i, i, i) for i in range(256)]
        frames = [{"rect": (0, 0, 16, 16), "pixels": bytes(range(256))}]
        with self.assertRaises(gifpics.GifError):
            gifpics.animation(gif(16, 16, many, frames))
        a = gifpics.animation(gif(16, 16, many, [{"rect": (0, 0, 16, 16), "pixels": bytes(range(255)) + b"\0"}]))
        self.assertEqual(len(a.palette), 255)

    def test_gif87a(self):
        a = gifpics.animation(gif(1, 1, PAL4, [{"rect": (0, 0, 1, 1), "pixels": bytes([2]), "control": False}],
                                  version=b"GIF87a"))
        self.assertEqual(a.palette, [(0, 255, 0)])
        self.assertEqual(a.pictures, [bytes([1])])


class SpritePackTest(unittest.TestCase):
    def walking(self, width, height, colours, frames):
        gct = [(i * 9 % 256, i * 5 % 256, i * 3 % 256) for i in range(colours)]
        gct += [(0, 0, 0)] * ((1 << max(1, (len(gct) - 1).bit_length())) - len(gct))
        out = []
        for f in range(frames):
            px = bytes(((r * 5 + c * 3 + f) % (colours - 1)) + 1 if (r * 3 + c) % 7 else 0
                       for r in range(height) for c in range(width))
            out.append({"rect": (0, 0, width, height), "pixels": px, "transparent": 0, "disposal": 2,
                        "delay": f % 3})
        return gif(width, height, gct, out)

    def test_round_trip_four_and_eight_bits(self):
        for colours, bpp in ((5, 4), (16, 4), (17, 8), (40, 8)):
            front = self.walking(21, 10, colours, 6)
            back = self.walking(8, 9, 4, 3)
            data, infos = sprite_pack.pack([front, back], "Walker")
            self.assertEqual(infos[0]["bpp"], bpp)
            self.assertEqual(infos[0]["colours"], colours - 1)
            self.assertEqual(sprite_pack.check(data, [front, back]), [])
            n, total, name = sprite_pack.header(data)
            self.assertEqual((n, total, name), (2, len(data), "Walker"))
            anims = sprite_pack.unpack(data)
            self.assertEqual([a.order[0][1] for a in anims], [10, 10])
            # Delays 0 and 1 play as 10 (100 ms); 2 stays.
            self.assertEqual([d for _, d in anims[0].order], [10, 10, 2, 10, 10, 2])

    def test_one_animation_and_empty_name(self):
        front = self.walking(3, 3, 3, 2)
        data, infos = sprite_pack.pack([front])
        self.assertEqual(sprite_pack.header(data), (1, len(data), ""))
        self.assertEqual(sprite_pack.check(data, [front]), [])

    def test_picture_zero_is_the_whole_canvas(self):
        data, _ = sprite_pack.pack([self.walking(7, 5, 6, 4)])
        base, = struct.unpack_from("<I", data, sprite_pack.HEADER)
        W, H, bpp, ncol, npic = struct.unpack_from("<HHBBH", data, base)
        tables = base + 16 + 3 * ncol + (-(3 * ncol) % 4)
        off, length, y0, rows = struct.unpack_from("<IHBB", data, tables)
        self.assertEqual((y0, rows), (0, H))
        self.assertEqual(off, data.index(data[off:off + length]))

    def test_corruption_is_seen(self):
        data, _ = sprite_pack.pack([self.walking(9, 9, 7, 5)], "X")
        for pos in (0, 9, 13, len(data) - 1):
            bad = bytearray(data)
            bad[pos] ^= 1
            with self.assertRaises(sprite_pack.PackError):
                sprite_pack.unpack(bytes(bad))
        with self.assertRaises(sprite_pack.PackError):
            sprite_pack.unpack(data[:-1])

    def test_check_reports_a_different_gif(self):
        a = self.walking(6, 6, 5, 3)
        b = self.walking(6, 6, 5, 4)
        data, _ = sprite_pack.pack([a])
        self.assertTrue(sprite_pack.check(data, [b]))
        self.assertTrue(sprite_pack.check(data, [a, a]))

    def test_name_rules(self):
        for name in ("A" * 16, "café", "tab\t"):
            with self.assertRaises(sprite_pack.PackError):
                sprite_pack.pack([self.walking(2, 2, 3, 1)], name)
        data, _ = sprite_pack.pack([self.walking(2, 2, 3, 1)], "A" * 15)
        self.assertEqual(sprite_pack.header(data)[2], "A" * 15)

    def test_pack_rows(self):
        pic = bytes([1, 2, 3, 4, 5, 6])
        packed = sprite_pack.pack_rows(pic, 3, 2, 4)
        self.assertEqual(packed, bytes([0x12, 0x30, 0x45, 0x60]))
        self.assertEqual(sprite_pack.unpack_rows(packed, 3, 2, 4), pic)
        self.assertEqual(sprite_pack.pack_rows(pic, 3, 2, 8), pic)


if __name__ == "__main__":
    unittest.main()
