#!/usr/bin/env python3
"""The sprite pack: one or two GIFs (front, back) as the pictures they show.

  sprite_pack.py pack [--name <text>] -o <out.spk> <front.gif> [<back.gif>]
  sprite_pack.py check <pack.spk> <front.gif> [<back.gif>]
  sprite_pack.py info <pack.spk>

`pack` writes the file and prints one line per animation
(`animation N: WxH bpp B colours C pictures P frames F bytes N`) and the total;
nothing it prints names the GIF or the sprite. `check` unpacks the file the
way the Prospector Dongle decodes it and compares every frame's pixels and
delay with the GIFs; `info` prints the header's sizes. Exit 0, 1 on a
mismatch, 2 on bad input. Standard library only, Python 3.9.

The format (docs/sprite-pack.md says why), little-endian, all offsets from the
start of the file:

  file     0 "SPK1"  4 u16 animations  6 u16 0  8 u32 total length
          12 u32 CRC-32 of [16, total)  16 char name[16] (ASCII, NUL-padded)
          32 u32 offset[animations], padded to a multiple of 4
  animation (at its offset, a multiple of 4)
           0 u16 width  2 u16 height  4 u8 bits per pixel (4 or 8)
           5 u8 colours (pixel values 1..colours; 0 is transparent)
           6 u16 pictures  8 u16 frames  10 u16 stride (bytes a row)  12 u32 0
          16 palette: colours x RGB, padded to a multiple of 4
             pictures x {u32 offset, u16 length, u8 first row, u8 rows}
             frames x {u16 picture, u16 delay in 10 ms}
  picture  a raw deflate stream (RFC 1951) of its rows [first row, first row +
           rows) in packed pixels (4 bpp: two a byte, high nibble first, a pad
           nibble ending an odd-width row); picture 0 is the whole canvas and
           the preset dictionary of every other picture.
"""
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gifpics  # noqa: E402

MAGIC = b"SPK1"
HEADER = 32
NAME_LEN = 15


def play_delay(delay):
    """A frame's delay in 10 ms as the device shows it: 0 and 1 play as 100 ms,
    as browsers do; everything else as the file says."""
    return delay if delay >= 2 else 10


class PackError(ValueError):
    pass


def pack_rows(pic, width, height, bpp):
    """A picture's rows in packed pixels (gifpics values)."""
    if bpp == 8:
        return pic
    out = bytearray()
    for r in range(height):
        row = pic[r * width:(r + 1) * width]
        if width % 2:
            row += b"\0"
        out += bytes(row[i] << 4 | row[i + 1] for i in range(0, len(row), 2))
    return bytes(out)


def unpack_rows(packed, width, height, bpp):
    if bpp == 8:
        return packed
    stride = (width + 1) // 2
    return b"".join(
        bytes(v for b in packed[r * stride:(r + 1) * stride] for v in (b >> 4, b & 15))[:width]
        for r in range(height))


def deflate(data, zdict=None):
    if zdict is None:
        c = zlib.compressobj(9, zlib.DEFLATED, -15, 9)
    else:
        c = zlib.compressobj(9, zlib.DEFLATED, -15, 9, zlib.Z_DEFAULT_STRATEGY, zdict)
    return c.compress(data) + c.flush()


def inflate(data, size, zdict=None):
    d = zlib.decompressobj(-15) if zdict is None else zlib.decompressobj(-15, zdict)
    out = d.decompress(data, size)
    if len(out) != size or d.unconsumed_tail:
        raise PackError("a stream does not decode to %d bytes" % size)
    return out


def check_name(name):
    if len(name) > NAME_LEN or not all(" " <= c <= "~" for c in name):
        raise PackError("the name must be printable ASCII, %d characters at most" % NAME_LEN)
    return name


def pack_animation(anim, base):
    """(the animation's bytes, its info dict) for an Animation at file offset base."""
    W, H = anim.width, anim.height
    ncol = len(anim.palette)
    if not 1 <= ncol <= 255:
        raise PackError("%d colours" % ncol)
    bpp = 4 if ncol <= 15 else 8
    stride = (W + 1) // 2 if bpp == 4 else W
    packed = [pack_rows(p, W, H, bpp) for p in anim.pictures]
    npic, nfr = len(packed), len(anim.order)
    if npic > 65535 or nfr > 65535 or H > 255 or W > 65535:
        raise PackError("too many pictures or frames, or too large a canvas")
    palette = b"".join(bytes(rgb) for rgb in anim.palette)
    palette += b"\0" * (-len(palette) % 4)
    tables = 16 + len(palette) + 8 * npic + 4 * nfr
    tables += -tables % 4

    streams = []
    entries = []
    pos = base + tables
    for i, p in enumerate(packed):
        if i == 0:
            y0, rows = 0, H
            stream = deflate(p)
        else:
            used = [r for r in range(H) if any(p[r * stride:(r + 1) * stride])]
            y0, rows = (used[0], used[-1] - used[0] + 1) if used else (0, 0)
            stream = deflate(p[y0 * stride:(y0 + rows) * stride], packed[0])
        if len(stream) > 65535:
            raise PackError("picture %d: a %d byte stream" % (i, len(stream)))
        entries.append(struct.pack("<IHBB", pos, len(stream), y0, rows))
        streams.append(stream)
        pos += len(stream)

    head = struct.pack("<HHBBHHHI", W, H, bpp, ncol, npic, nfr, stride, 0)
    frames = b"".join(struct.pack("<HH", pic, play_delay(delay)) for pic, delay in anim.order)
    body = head + palette + b"".join(entries) + frames
    body += b"\0" * (tables - len(body))
    info = {"width": W, "height": H, "bpp": bpp, "colours": ncol, "pictures": npic,
            "frames": nfr, "stride": stride, "bytes": tables + sum(map(len, streams))}
    return body + b"".join(streams), info


def pack(gifs, name=""):
    """(file contents, [info per animation]) for the GIF file contents in gifs."""
    if not 1 <= len(gifs) <= 2:
        raise PackError("one or two GIFs")
    check_name(name)
    anims = [gifpics.animation(g) for g in gifs]
    n = len(anims)
    first = HEADER + 4 * n
    first += -first % 4
    parts = []
    infos = []
    offsets = []
    pos = first
    for anim in anims:
        offsets.append(pos)
        part, info = pack_animation(anim, pos)
        part += b"\0" * (-len(part) % 4)
        parts.append(part)
        infos.append(info)
        pos += len(part)
    total = pos
    rest = name.encode("ascii").ljust(16, b"\0")
    rest += b"".join(struct.pack("<I", o) for o in offsets)
    rest += b"\0" * (first - HEADER - 4 * n)
    rest += b"".join(parts)
    head = MAGIC + struct.pack("<HHII", n, 0, total, zlib.crc32(rest))
    out = head + rest
    assert len(out) == total
    return out, infos


def header(data):
    """(animations, total, name) of a pack's header; PackError when it is not one."""
    if data[:4] != MAGIC:
        raise PackError("not a sprite pack")
    n, _, total, crc = struct.unpack_from("<HHII", data, 4)
    if n == 0 or total != len(data) or zlib.crc32(data[16:total]) != crc:
        raise PackError("the length or CRC-32 does not match")
    name = data[16:32].split(b"\0", 1)[0].decode("ascii")
    return n, total, name


def unpack(data):
    """The animations in a pack as gifpics.Animation objects, decoded as the device does."""
    n, _, _ = header(data)
    anims = []
    for a in range(n):
        base, = struct.unpack_from("<I", data, HEADER + 4 * a)
        W, H, bpp, ncol, npic, nfr, stride, _ = struct.unpack_from("<HHBBHHHI", data, base)
        pos = base + 16
        palette = [tuple(data[pos + 3 * i:pos + 3 * i + 3]) for i in range(ncol)]
        pos += 3 * ncol + (-(3 * ncol) % 4)
        entries = [struct.unpack_from("<IHBB", data, pos + 8 * i) for i in range(npic)]
        pos += 8 * npic
        order = [struct.unpack_from("<HH", data, pos + 4 * i) for i in range(nfr)]
        zdict = None
        pictures = []
        for i, (off, length, y0, rows) in enumerate(entries):
            band = inflate(data[off:off + length], rows * stride, zdict)
            packed = b"\0" * (y0 * stride) + band + b"\0" * ((H - y0 - rows) * stride)
            if i == 0:
                zdict = packed
            pictures.append(unpack_rows(packed, W, H, bpp))
        anims.append(gifpics.Animation(W, H, palette, pictures, order))
    return anims


def check(data, gifs):
    """The mismatches, as strings, between a pack and the GIFs it was packed from."""
    bad = []
    anims = unpack(data)
    if len(anims) != len(gifs):
        return ["%d animations for %d GIFs" % (len(anims), len(gifs))]
    for a, (mine, g) in enumerate(zip(anims, gifs)):
        ref = gifpics.animation(g)
        if (mine.width, mine.height) != (ref.width, ref.height):
            bad.append("animation %d: %dx%d, the GIF %dx%d" % (a, mine.width, mine.height, ref.width, ref.height))
            continue
        if mine.palette != ref.palette:
            bad.append("animation %d: the palette differs" % a)
        if len(mine.order) != len(ref.order):
            bad.append("animation %d: %d frames, the GIF %d" % (a, len(mine.order), len(ref.order)))
            continue
        for i, ((p1, d1), (p2, d2)) in enumerate(zip(mine.order, ref.order)):
            if mine.pictures[p1] != ref.pictures[p2]:
                bad.append("animation %d frame %d: the pixels differ" % (a, i))
            if d1 != play_delay(d2):
                bad.append("animation %d frame %d: delay %d, the GIF %d" % (a, i, d1, d2))
    return bad


def _read(path):
    with open(path, "rb") as f:
        return f.read()


def main(argv):
    if len(argv) >= 4 and argv[0] == "pack":
        args = argv[1:]
        name = ""
        if args[0] == "--name":
            name, args = args[1], args[2:]
        if len(args) < 3 or args[0] != "-o":
            print(__doc__, file=sys.stderr)
            return 2
        try:
            data, infos = pack([_read(p) for p in args[2:]], name)
        except (gifpics.GifError, PackError) as e:
            print("sprite_pack: %s" % e, file=sys.stderr)
            return 2
        with open(args[1], "wb") as f:
            f.write(data)
        for i, info in enumerate(infos):
            print("animation %d: %dx%d bpp %d colours %d pictures %d frames %d bytes %d" % (
                i, info["width"], info["height"], info["bpp"], info["colours"],
                info["pictures"], info["frames"], info["bytes"]))
        print("total %d bytes" % len(data))
        return 0
    if len(argv) >= 3 and argv[0] == "check":
        try:
            bad = check(_read(argv[1]), [_read(p) for p in argv[2:]])
        except (gifpics.GifError, PackError) as e:
            print("sprite_pack: %s" % e, file=sys.stderr)
            return 2
        for line in bad:
            print(line)
        print("check: %s" % ("ok" if not bad else "%d mismatches" % len(bad)))
        return 1 if bad else 0
    if len(argv) == 2 and argv[0] == "info":
        try:
            data = _read(argv[1])
            n, total, name = header(data)
            anims = unpack(data)
        except PackError as e:
            print("sprite_pack: %s" % e, file=sys.stderr)
            return 2
        print("%d animations, %d bytes, name of %d characters" % (n, total, len(name)))
        for i, a in enumerate(anims):
            print("animation %d: %dx%d colours %d pictures %d frames %d" % (
                i, a.width, a.height, len(a.palette), len(a.pictures), len(a.order)))
        return 0
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
